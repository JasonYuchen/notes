# seastar中的执行引擎

## Reactor模式

`TODO`

seastar中的reactor实现主要在这四个文件中`seastar/core/reactor.hh, src/core/reactor.cc, src/core/reactor_backend.hh, src/core/reactor_backend.cc`，以及一个作为接口的`pollfn`位于`seastar/core/internal/poll.hh`

## `reactor`和`pollfn`

reactor模式的核心流程就是`eventloop: repeat(poll tasks -> execute tasks)`，而seastar中设计了多个pollable的对象，抽象为`struct pollfn`，并设计了reactor引擎`reactor`

### `struct pollfn`

从上述介绍可以看出，reactor模式需要能够poll，因此seastar中将其抽象成一个接口`pollfn`，实现了这个接口的类就认为是pollable，并且可以在reactor引擎中使用

`pollfn`的核心就是一个`poll()`函数（配合`pure_poll()`辅助使用），而`try_enter_interrput_mode()/exit_interrupt_mode()`是用于系统**低负载时避免循环轮询而进入中断唤醒**模式（一些细节可[见此](Membarrier_Adventures.md)）

```cpp
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

### `class reactor`

reactor引擎的实际使用与执行流程都在`class reactor`中，而支撑其实现的底层可以是Linux AIO、epoll、OSv或io uring，reactor引擎中较为重要的几个函数如下：

- `reactor::run()`
  reactor模式的核心事件循环就在于`reactor::run()`，包含了注册所有pollable对象、注册reactor观测指标、初始化各类资源等，见代码和注释

  多个不同任务的poller都会被reactor检查，且poller之前的任务有可能存在较优的执行顺序，因此检查和**执行poller的顺序对reactor整体性能影响非常大**

  另外seastar的reactor引擎还支持**轮询polling**和**中断响应interrupt-driven**两种任务执行策略，前者适合高负载环境，可以充分降低中断引入的延迟，后者适合低负载环境，可以避免轮询带来的不必要性能开销

    ```cpp
    int reactor::run() {
        ////// skip some codes //////

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
        ////// skip some codes //////

        // cpu和net的初始化流程如下：
        // 1. configure的时候创建network模块，_network_stack_ready标志network就绪情况
        // 2. network就绪后，通过_cpu_started.signal()通知就绪
        // 3. 当所有cpu都就绪后，即_cpu_started.wait(smp::count)等待结束，就会执行network初始化
        // 4. _network_stack->initialize()初始化完成后，reactor引擎就绪，设置_start_promise.set_value()
        // 随后，对reactor引擎可以调用reactor::when_started()来确认引擎就绪，并执行自定义的task，例如：
        // (void)engine().when_started().then([&srv] { srv.run(); });

        // Start initialization in the background.
        // Communicate when done using _start_promise.
        // 在后台使用信号量_cpu_started.wait()异步等待CPU就绪
        (void)_cpu_started.wait(smp::count).then([this] {
            (void)_network_stack->initialize().then([this] {
                _start_promise.set_value();
            });
        });
        // Wait for network stack in the background and then signal all cpus.
        // 等待network创建就绪，随后就运行.then()通知CPU就绪
        (void)_network_stack_ready->then([this] (std::unique_ptr<network_stack> stack) {
            _network_stack = std::move(stack);
            return smp::invoke_on_all([] {
                engine()._cpu_started.signal();
            });
        });
        ////// skip some codes //////
        bool idle = false;

        std::function<bool()> check_for_work = [this] () {
            return poll_once() || have_more_tasks();
        };
        std::function<bool()> pure_check_for_work = [this] () {
            return pure_poll_once() || have_more_tasks();
        };
        // 所有准备工作就绪，真正进入reactor的eventloop
        while (true) {
            run_some_tasks();
            if (_stopped) {
                ////// skip some codes //////
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
                ////// skip some codes //////
                if (go_to_sleep) {
                    internal::cpu_relax();
                    if (idle_end - idle_start > _max_poll_time) {
                        ////// skip some codes
                        // 从轮询处理转为进入中断唤醒模式，等待新事件唤醒reactor引擎
                        sleep();
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

- `reactor::poll_once() & reactor::pure_poll_once()`
  检查每个poller是否有事件发生需要处理，**差别在于`pollfn::poll()`用于检查任务是否已经被完成，而`pollfn::pure_poll()`用于检查是否有任务需要做**，例如在计时器任务的`reactor::lowres_timer_pollfn`中，前者会检查并实际执行到期的计时器任务，而后者只会检查是否有到期的计时器任务，另外有一些poller例如`reactor::io_queue_submission_pollfn`下这两个函数实际都会执行任务

  因此在reactor的这两个函数中，`poll_once()`会执行每个poller的`poll()`从而确保所有有任务的poller都可以得到执行，而`pure_poll_once()`只是检查是否有任务，任意一个poller存在可以执行的任务就会直接返回

    ```cpp
    bool reactor::poll_once() {
        bool work = false;
        for (auto c : _pollers) {
            work |= c->poll();
        }
        return work;
    }

    bool reactor::pure_poll_once() {
        for (auto c : _pollers) {
            if (c->pure_poll()) {
                return true;
            }
        }
        return false;
    }
    ```

- `reactor::have_more_tasks()`
  在`run()`中检查是否有任务的方式是`poll_once()/pure_poll_once() || have_more_tasks()`，实际上就是检查reactor所拥有的两个任务队列的队列中是否有任务队列存在，这两个队列来接收各种任务队列（**类似于双缓冲**，每次有新任务队列时都加入`_activating_task_queues`，而在reactor运行时该对列会被一次性移动到`_active_task_queues`中），在后面`add_task`中会用到

    ```cpp
    inline bool reactor::have_more_tasks() const {
        return _active_task_queues.size() + _activating_task_queues.size();
    }
    ```

- `reactor::run_some_tasks()`
  该函数实际执行流程为**将activating queues移动到active queues中，并在被抢占前`need_preempt() == false`连续运行task queue的任务**，下列代码还包含了各种统计信息、CPU暂停检测等代码

    ```cpp
    void reactor::run_some_tasks() {
        if (!have_more_tasks()) {
            return;
        }
        sched_print("run_some_tasks: start");
        reset_preemption_monitor();

        sched_clock::time_point t_run_completed = std::chrono::steady_clock::now();
        STAP_PROBE(seastar, reactor_run_tasks_start);
        _cpu_stall_detector->start_task_run(t_run_completed);
        do {
            auto t_run_started = t_run_completed;
            // 1. 这里就会将所有activating queues移动到active queues中
            insert_activating_task_queues();
            task_queue* tq = pop_active_task_queue(t_run_started);
            sched_print("running tq {} {}", (void*)tq, tq->_name);
            tq->_current = true;
            _last_vruntime = std::max(tq->_vruntime, _last_vruntime);
            // 2. 执行当前task queue中的所有任务
            run_tasks(*tq);
            tq->_current = false;
            t_run_completed = std::chrono::steady_clock::now();
            auto delta = t_run_completed - t_run_started;
            account_runtime(*tq, delta);
            sched_print("run complete ({} {}); time consumed {} usec; final vruntime {} empty {}",
                    (void*)tq, tq->_name, delta / 1us, tq->_vruntime, tq->_q.empty());
            tq->_ts = t_run_completed;
            // 若本次没有执行完当前task queue的所有任务，将此task queue重新加入到active queues下次继续执行
            if (!tq->_q.empty()) {
                insert_active_task_queue(tq);
            } else {
                tq->_active = false;
            }
            // 3. continue executing tasks if not preempted
        } while (have_more_tasks() && !need_preempt());
        _cpu_stall_detector->end_task_run(t_run_completed);
        STAP_PROBE(seastar, reactor_run_tasks_end);
        *internal::current_scheduling_group_ptr() = default_scheduling_group(); // Prevent inheritance from last group run
        sched_print("run_some_tasks: end");
    }
    ```

- `reactor::run_tasks()`
  运行任务的核心就是调用`task::run_and_dispose()`，一次跨核任务的[执行示例](Message_Passing.md)中，第五步省略的reactor执行过程就是这里

    ```cpp
    void reactor::run_tasks(task_queue& tq) {
        // Make sure new tasks will inherit our scheduling group
        *internal::current_scheduling_group_ptr() = scheduling_group(tq._id);
        auto& tasks = tq._q;
        while (!tasks.empty()) {
            auto tsk = tasks.front();
            tasks.pop_front();
            STAP_PROBE(seastar, reactor_run_tasks_single_start);
            task_histogram_add_task(*tsk);
            _current_task = tsk;
            tsk->run_and_dispose();
            _current_task = nullptr;
            STAP_PROBE(seastar, reactor_run_tasks_single_end);
            ++tq._tasks_processed;
            ++_global_tasks_processed;
            // check at end of loop, to allow at least one task to run
            if (need_preempt()) {
                if (tasks.size() <= _max_task_backlog) {
                    break;
                } else {
                    // While need_preempt() is set, task execution is inefficient due to
                    // need_preempt() checks breaking out of loops and .then() calls. See
                    // #302.
                    reset_preemption_monitor();
                }
            }
        }
    }
    ```

- `reactor::sleep() & reactor::wakeup()`
  **当负载较低保持一定时间后，reactor引擎就会自动转入中断处理模式**，此时每个poller都要进入中断模式，中断模式下reactor的唤醒则是通过监听的`_notify_eventfd`来触发的，中断模式具体的中断方式由底层reactor backend实现，例如`reactor_backend_epoll::wait_and_process_events()`中就是使用了Linux Epoll的`epoll_wait`，`reactor_backend_aio::wait_and_process_events()`中则是使用了Linux AIO的`io_pgetevents`

  底层都会监听`_notify_eventfd`从而可以在上层调用`reactor::wakeup()`时唤醒reactor backend从`wait_and_process_events`中恢复轮询执行模式

    ```cpp
    void reactor::sleep() {
        for (auto i = _pollers.begin(); i != _pollers.end(); ++i) {
            auto ok = (*i)->try_enter_interrupt_mode();
            if (!ok) {
                // 回滚此前已进入sleep的poller，所有poller必须保持一致
                while (i != _pollers.begin()) {
                    (*--i)->exit_interrupt_mode();
                }
                return;
            }
        }

        // 进入底层reactor_backend的sleep模式
        _backend->wait_and_process_events(&_active_sigmask);

        // 被唤醒后离开sleep模式并开始处理事件
        for (auto i = _pollers.rbegin(); i != _pollers.rend(); ++i) {
            (*i)->exit_interrupt_mode();
        }
    }

    void reactor::wakeup() {
        uint64_t one = 1;
        ::write(_notify_eventfd.get(), &one, sizeof(one));
    }
    ```

reactor的上层使用者实际提交各种任务时，会使用的函数：

- `schedule(task* t) & reactor::add_task(task* t)`
  `add_task`实际上只是根据调度组id将task加入对应的task queue，对应的`add_urgent_task`在流程上完全一样，但是与此不同的是后者在函数体内额外使用了`memory::scoped_critical_alloc_section _`，含义是**此作用域内不应该出现内存分配失败**，如果在此作用域内出现分配失败并且启用了内存诊断，就会dump出内存诊断报告

    ```cpp
    void schedule(task* t) noexcept {
        engine().add_task(t);
    }

    void add_task(task* t) noexcept {
        // 不同的task分属于不同的scheduling group
        // 每个scheduling group根据id可以获得自己的task queue
        // 同时每个scheduling group可以设置运行任务的配额share，来实现不同group之间的调度运行
        auto sg = t->group();
        auto* q = _task_queues[sg._id].get();
        bool was_empty = q->_q.empty();
        q->_q.push_back(std::move(t));
    #ifdef SEASTAR_SHUFFLE_TASK_QUEUE
        shuffle(q->_q.back(), *q);
    #endif
        if (was_empty) {
            // 若原先是没有任务的，此时加入了任务就需要activate加入activating queues
            activate(*q);
        }
    }
    ```

- `reactor::activate(task_queue& tq)`
  `activate`只会在`add_task/add_urgent_task`中被使用，用于原先没有task的task queue在收到task时直接激活加入`_activating_task_queues`，并会更新一些统计信息，另外因为此函数是task queue在没有任务时收到第一个任务时会被调用，通常是network/disk I/O的任务而不是CPU任务

    ```cpp
    void reactor::activate(task_queue& tq) {
        if (tq._active) {
            return;
        }
        sched_print("activating {} {}", (void*)&tq, tq._name);
        // If activate() was called, the task queue is likely network-bound or I/O bound, not CPU-bound. As
        // such its vruntime will be low, and it will have a large advantage over other task queues. Limit
        // the advantage so it doesn't dominate scheduling for a long time, in case it _does_ become CPU
        // bound later.
        //
        // FIXME: different scheduling groups have different sensitivity to jitter, take advantage
        if (_last_vruntime > tq._vruntime) {
            sched_print("tq {} {} losing vruntime {} due to sleep", (void*)&tq, tq._name, _last_vruntime - tq._vruntime);
        }
        tq._vruntime = std::max(_last_vruntime, tq._vruntime);
        auto now = std::chrono::steady_clock::now();
        tq._waittime += now - tq._ts;
        tq._ts = now;
        _activating_task_queues.push_back(&tq);
    }
    ```

reactor另外提供的一些功能：

- `reactor::add_timer/del_timer`
  计时器相关的功能，其实现就是调用了reactor backend的`arm_highres_timer`，添加一个timer的流程如下：

    ```cpp
    void reactor::add_timer(timer<steady_clock_type>* tmr) noexcept {
        // 所有timers由timer_set数据结构进行管理，当新增的timer其过期时间小于当前最近过期时间时
        // 就需要直接调用enable_timer进行更新backend timerfd的触发时间
        if (queue_timer(tmr)) {
            enable_timer(_timers.get_next_timeout());
        }
    }

    bool reactor::queue_timer(timer<steady_clock_type>* tmr) noexcept {
        // timer_set针对大量reschedule和cancel任务做了额外优化：延迟对timer的过期时间排序直到有timer触发
        // 理由是正常工作负载下expiration非常少见
        return _timers.insert(*tmr);
    }

    void reactor::enable_timer(steady_clock_type::time_point when) noexcept
    {
        itimerspec its;
        its.it_interval = {};
        its.it_value = to_timespec(when);
        _backend->arm_highres_timer(its);
    }
    ```

## `reactor_backend`

`reactor_backend`实际上实现了上述reactor引擎所需的各种功能接口，而底层有多种实现，在seastar中有基于Linux AIO的`reactor_backend_aio`、基于Linux epoll的`reactor_backend_epoll`、基于OSv的`reactor_backend_osv`

[io uring](https://github.com/JasonYuchen/notes/blob/master/linux/io_uring.md)是Linux新一代的IO机制，解决了Linux AIO的诸多问题，seastar后续也将加入[基于Linux io uring的实现](https://www.scylladb.com/2020/05/05/how-io_uring-and-ebpf-will-revolutionize-programming-in-linux/)，因此这里主要分析更为接近的Linux AIO实现`reactor_backend_aio`（`TODO:`后续待seastar完善基于io uring的实现时会补充相关内容）

### `class reactor_backend_aio`

`TODO`

### `class reactor_backend_epoll`

`TODO`

### `class reactor_backend_iouring`

`TODO`
