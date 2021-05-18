# seastar中的执行引擎

## Reactor模式

`TODO`

seastar中的reactor实现主要在这四个文件中`seastar/core/reactor.hh, src/core/reactor.cc, src/core/reactor_backend.hh, src/core/reactor_backend.cc`，以及一个作为接口的`pollfn`位于`seastar/core/internal/poll.hh`

## `reactor`和`pollfn`

- `struct pollfn`
  从上述介绍可以看出，reactor模式需要能够poll，因此seastar中将其抽象成一个接口`pollfn`，实现了这个接口的类就认为是pollable，并且可以在reactor引擎中使用
  
  `pollfn`的核心就是一个`poll()`函数（配合`pure_poll()`辅助使用），而`try_enter_interrput_mode()/exit_interrupt_mode()`是用于系统低负载时避免循环轮询而进入中断唤醒模式（一些细节可[见此](Membarrier_Adventures.md)）

    ```c++
    // seastar/core/internal/poll.hh
    struct pollfn {
        virtual ~pollfn() {}
        // Returns true if work was done (false = idle)
        virtual bool poll() = 0;
        // Checks if work needs to be done, but without actually doing any
        // returns true if works needs to be done (false = idle)
        virtual bool pure_poll() = 0;
        // Tries to enter interrupt mode.
        //
        // If it returns true, then events from this poller will wake
        // a sleeping idle loop, and exit_interrupt_mode() must be called
        // to return to normal polling.
        //
        // If it returns false, the sleeping idle loop may not be entered.
        virtual bool try_enter_interrupt_mode() = 0;
        virtual void exit_interrupt_mode() = 0;
    };
    ```

  实现了此接口的主要类有：
  - `execution_stage_pollfn`：`TODO`
  - `io_queue_submission_pollfn`：用于轮询I/O队列提交情况，若有空位就可以发起新的I/O请求
  - `lowres_timer_pollfn`：用于轮询低精度的定时器是否有超时
  - `reactor_stall_sampler`：用于采样reactor引擎的stall情况
  - `reap_kernel_completions_pollfn`：用于轮询内核存储层的任务完成情况，Linux kernel自5.0开始引入了全新的异步API io_uring，详细分析[见此](https://github.com/JasonYuchen/notes/blob/master/linux/io_uring.md)
  - `signal_pollfn`：用于轮询信号量的情况
  - `smp_pollfn`：用于轮询是否有其他线程过来的任务，seastar采用message-passing的方式进行跨线程任务调度，详细分析[见此](Message_Passing.md)
  - `syscall_pollfn`：`TODO`
  - `batch_flush_pollfn`：`TODO`
  - `drain_cross_cpu_freelist_pollfn`：用于轮询是否有其他线程传递过来要求本线程释放资源的任务
  - `kernel_submit_work_pollfn`：用于轮询提交I/O任务给内核

- `reactor::run()`
  reactor模式的核心事件循环就在于`reactor::run()`，见代码和注释：

    ```C++
    int reactor::run() {
    #ifndef SEASTAR_ASAN_ENABLED
        // SIGSTKSZ is too small when using asan. We also don't need to
        // handle SIGSEGV ourselves when using asan, so just don't install
        // a signal handler stack.
        auto signal_stack = install_signal_handler_stack();
    #else
        (void)install_signal_handler_stack;
    #endif

        register_metrics();

        // The order in which we execute the pollers is very important for performance.
        //
        // This is because events that are generated in one poller may feed work into others. If
        // they were reversed, we'd only be able to do that work in the next task quota.
        //
        // One example is the relationship between the smp poller and the I/O submission poller:
        // If the smp poller runs first, requests from remote I/O queues can be dispatched right away
        //
        // We will run the pollers in the following order:
        //
        // 1. SMP: any remote event arrives before anything else
        // 2. reap kernel events completion: storage related completions may free up space in the I/O
        //                                   queue.
        // 4. I/O queue: must be after reap, to free up events. If new slots are freed may submit I/O
        // 5. kernel submission: for I/O, will submit what was generated from last step.
        // 6. reap kernel events completion: some of the submissions from last step may return immediately.
        //                                   For example if we are dealing with poll() on a fd that has events.
        poller smp_poller(std::make_unique<smp_pollfn>(*this));

        poller reap_kernel_completions_poller(std::make_unique<reap_kernel_completions_pollfn>(*this));
        poller io_queue_submission_poller(std::make_unique<io_queue_submission_pollfn>(*this));
        poller kernel_submit_work_poller(std::make_unique<kernel_submit_work_pollfn>(*this));
        poller final_real_kernel_completions_poller(std::make_unique<reap_kernel_completions_pollfn>(*this));

        poller batch_flush_poller(std::make_unique<batch_flush_pollfn>(*this));
        poller execution_stage_poller(std::make_unique<execution_stage_pollfn>());

        start_aio_eventfd_loop();

        // 处理SIGINT和SIGTERM信号，收到时就停止reactor引擎的运行
        if (_id == 0 && _cfg.auto_handle_sigint_sigterm) {
            if (_handle_sigint) {
                _signals.handle_signal_once(SIGINT, [this] { stop(); });
            }
            _signals.handle_signal_once(SIGTERM, [this] { stop(); });
        }

        // Start initialization in the background.
        // Communicate when done using _start_promise.
        (void)_cpu_started.wait(smp::count).then([this] {
            (void)_network_stack->initialize().then([this] {
                _start_promise.set_value();
            });
        });
        // Wait for network stack in the background and then signal all cpus.
        (void)_network_stack_ready->then([this] (std::unique_ptr<network_stack> stack) {
            _network_stack = std::move(stack);
            return smp::invoke_on_all([] {
                engine()._cpu_started.signal();
            });
        });

        poller syscall_poller(std::make_unique<syscall_pollfn>(*this));

        poller drain_cross_cpu_freelist(std::make_unique<drain_cross_cpu_freelist_pollfn>());

        poller expire_lowres_timers(std::make_unique<lowres_timer_pollfn>(*this));
        poller sig_poller(std::make_unique<signal_pollfn>(*this));

        using namespace std::chrono_literals;
        timer<lowres_clock> load_timer;
        auto last_idle = _total_idle;
        auto idle_start = sched_clock::now(), idle_end = idle_start;
        load_timer.set_callback([this, &last_idle, &idle_start, &idle_end] () mutable {
            _total_idle += idle_end - idle_start;
            auto load = double((_total_idle - last_idle).count()) / double(std::chrono::duration_cast<sched_clock::duration>(1s).count());
            last_idle = _total_idle;
            load = std::min(load, 1.0);
            idle_start = idle_end;
            _loads.push_front(load);
            if (_loads.size() > 5) {
                auto drop = _loads.back();
                _loads.pop_back();
                _load -= (drop/5);
            }
            _load += (load/5);
        });
        load_timer.arm_periodic(1s);

        itimerspec its = seastar::posix::to_relative_itimerspec(_task_quota, _task_quota);
        _task_quota_timer.timerfd_settime(0, its);
        auto& task_quote_itimerspec = its;

        struct sigaction sa_block_notifier = {};
        sa_block_notifier.sa_handler = &reactor::block_notifier;
        sa_block_notifier.sa_flags = SA_RESTART;
        auto r = sigaction(cpu_stall_detector::signal_number(), &sa_block_notifier, nullptr);
        assert(r == 0);

        bool idle = false;

        std::function<bool()> check_for_work = [this] () {
            return poll_once() || have_more_tasks();
        };
        std::function<bool()> pure_check_for_work = [this] () {
            return pure_poll_once() || have_more_tasks();
        };
        while (true) {
            run_some_tasks();
            if (_stopped) {
                load_timer.cancel();
                // Final tasks may include sending the last response to cpu 0, so run them
                while (have_more_tasks()) {
                    run_some_tasks();
                }
                while (!_at_destroy_tasks->_q.empty()) {
                    run_tasks(*_at_destroy_tasks);
                }
                _finished_running_tasks = true;
                smp::arrive_at_event_loop_end();
                if (_id == 0) {
                    smp::join_all();
                }
                break;
            }

            _polls++;

            if (check_for_work()) {
                if (idle) {
                    _total_idle += idle_end - idle_start;
                    account_idle(idle_end - idle_start);
                    idle_start = idle_end;
                    idle = false;
                }
            } else {
                idle_end = sched_clock::now();
                if (!idle) {
                    idle_start = idle_end;
                    idle = true;
                }
                bool go_to_sleep = true;
                try {
                    // we can't run check_for_work(), because that can run tasks in the context
                    // of the idle handler which change its state, without the idle handler expecting
                    // it.  So run pure_check_for_work() instead.
                    auto handler_result = _idle_cpu_handler(pure_check_for_work);
                    go_to_sleep = handler_result == idle_cpu_handler_result::no_more_work;
                } catch (...) {
                    report_exception("Exception while running idle cpu handler", std::current_exception());
                }
                if (go_to_sleep) {
                    internal::cpu_relax();
                    if (idle_end - idle_start > _max_poll_time) {
                        // Turn off the task quota timer to avoid spurious wakeups
                        struct itimerspec zero_itimerspec = {};
                        _task_quota_timer.timerfd_settime(0, zero_itimerspec);
                        auto start_sleep = sched_clock::now();
                        _cpu_stall_detector->start_sleep();
                        sleep();
                        _cpu_stall_detector->end_sleep();
                        // We may have slept for a while, so freshen idle_end
                        idle_end = sched_clock::now();
                        _total_sleep += idle_end - start_sleep;
                        _task_quota_timer.timerfd_settime(0, task_quote_itimerspec);
                    }
                } else {
                    // We previously ran pure_check_for_work(), might not actually have performed
                    // any work.
                    check_for_work();
                }
            }
        }
        // To prevent ordering issues from rising, destroy the I/O queue explicitly at this point.
        // This is needed because the reactor is destroyed from the thread_local destructors. If
        // the I/O queue happens to use any other infrastructure that is also kept this way (for
        // instance, collectd), we will not have any way to guarantee who is destroyed first.
        _io_queues.clear();
        return _return;
    }
    ```

## `reactor_backend`

## `sleep`和`wake_up`
