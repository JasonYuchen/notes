# seastar中的执行引擎

## Reactor模式

`TODO`

seastar中的reactor实现主要在这四个文件中`seastar/core/reactor.hh, src/core/reactor.cc, src/core/reactor_backend.hh, src/core/reactor_backend.cc`，以及一个作为接口的`pollfn`位于`seastar/core/internal/poll.hh`

## `reactor`和`pollfn`

reactor模式的核心流程就是`eventloop: repeat(poll tasks -> execute tasks)`，而seastar中设计了多个pollable的对象，抽象为`struct pollfn`，并设计了reactor引擎`reactor`

### `struct pollfn`

从上述介绍可以看出，reactor模式需要能够poll，因此seastar中将其抽象成一个接口`pollfn`，实现了这个接口的类就认为是pollable，并且可以在reactor引擎中使用

`pollfn`的核心就是一个`poll()`函数（配合`pure_poll()`辅助使用），而`try_enter_interrput_mode()/exit_interrupt_mode()`是用于系统**低负载时避免循环轮询而进入中断唤醒**模式（一些细节可[见此](Membarrier_Adventures.md)）

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

### `class reactor`

reactor引擎的实际使用与执行流程都在`class reactor`中，而支撑其实现的底层可以是Linux AIO、epoll、OSv或io uring，reactor引擎中较为重要的几个函数如下：

- `reactor::run()`
  reactor模式的核心事件循环就在于`reactor::run()`，包含了注册所有pollable对象、注册reactor观测指标、初始化各类资源等，见代码和注释

  多个不同任务的poller都会被reactor检查，且poller之前的任务有可能存在较优的执行顺序，因此检查和**执行poller的顺序对reactor整体性能影响非常大**

  另外seastar的reactor引擎还支持**轮询polling**和**中断响应interrupt-driven**两种任务执行策略，前者适合高负载环境，可以充分降低中断引入的延迟，后者适合低负载环境，可以避免轮询带来的不必要性能开销

    ```C++
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

    ```c++
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

    ```C++
    inline bool reactor::have_more_tasks() const {
        return _active_task_queues.size() + _activating_task_queues.size();
    }
    ```

- `reactor::run_some_tasks()`

    ```C++
    TODO
    ```

- `reactor::run_tasks()`

    ```C++
    TODO
    ```

- `reactor::sleep() & reactor::wakeup()`

    ```C++
    TODO
    ```

- `reactor::activate(task_queue& tq)`

    ```C++
    TODO
    ```

reactor的使用者实际提交各种任务的函数：

- `schedule(task* t)`

    ```C++
    TODO
    ```

- `reactor::add_task(task* t)`

    ```C++
    TODO
    ```

## `reactor_backend`

`reactor_backend`实际上实现了上述reactor引擎所需的各种功能接口，而底层有多种实现，在seastar中有基于Linux AIO的`reactor_backend_aio`、基于Linux epoll的`reactor_backend_epoll`、基于OSv的`reactor_backend_osv`

[io uring](https://github.com/JasonYuchen/notes/blob/master/linux/io_uring.md)是Linux新一代的IO机制，解决了Linux AIO的诸多问题，seastar后续也将加入[基于Linux io uring的实现](https://www.scylladb.com/2020/05/05/how-io_uring-and-ebpf-will-revolutionize-programming-in-linux/)，因此这里主要分析与io uring更为相近的`reactor_backend_aio`（`TODO:`后续待seastar完善基于io uring的实现时会补充相关内容）
