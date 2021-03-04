# seastar中的协程

## 在seastar中使用协程

C++ 20中引入了协程的语言支持，协程相关内容可以[见此](https://github.com/JasonYuchen/notes/tree/master/coroutine)

seastar作为一个高性能异步编程框架，天然适合通过协程来使用

> seastar可以通过gcc和clang编译并使用协程，要求clang >= 10，而gcc暂时存在[sanitizer不支持协程](https://gcc.gnu.org/bugzilla/show_bug.cgi?id=95137)的问题（使用AddressSanitizer运行时会出现段错误）

示例使用如下，摘自[seastar教程](https://github.com/scylladb/seastar/blob/master/doc/tutorial.md#coroutines)：

```C++
#include <seastar/core/coroutine.hh>

seastar::future<int> read();
seastar::future<> write(int n);

seastar::future<int> slow_fetch_and_increment() {
    auto n = co_await read();     // #1
    co_await seastar::sleep(1s);  // #2
    auto new_n = n + 1;           // #3
    co_await write(new_n);        // #4
    co_return n;                  // #5
}
```

## `future` & `promise`

本文尝试结合seastar中`future`/`promise`对协程的支持，分析在seastar中使用协程时的执行流程

seastar对协程的支持主要在`<seastar/core/coroutine.hh>`中，略过模板对不同类型支持导致的重复代码，核心类有如下几个：

- `class coroutine_traits_base`
- `class coroutine_traits`
- `struct awaiter`
- `auto operator co_await`

### `co_await seastar::sleep(1s)`时发生了什么？

按以下两种情况分别来看当`co_await`一个`future`对象时seastar的行为

#### 1. 当这个`future`对象已经完成时

例如`seastar::make_ready_future<int>(3);`

0. 编译器会采用`coroutine_traits::promise_type`类型作为协程的`promise`类型，对于seastar即是定义在`<seastar/core/coroutine.hh>`中的`coroutine_traits_base::promise_type`，实际上封装了seastar自己的`seastar::promise`类型（定义在`<seastar/core/future.hh>`）

1. 在协程中等待一个对象，会首先需要构造出对应的`Awaiter`对象，[见此](https://github.com/JasonYuchen/notes/blob/master/coroutine/Cppcoro_Understanding_co_await.md#1-obtaining-the-awaiter)，seastar中的协程`promise`类型并沒有定义`await_transform`，则此时`future`就会通过重载的`co_await`转换为`Awaiter`对象

    ```C++
    template<typename... T>
    auto operator co_await(future<T...> f) noexcept {
        return internal::awaiter<T...>(std::move(f));
    }
    ```

2. 随后就会由编译器转换为[这种执行流](https://github.com/JasonYuchen/notes/blob/master/coroutine/Cppcoro_Understanding_co_await.md#2-awaiting-the-awaiter)

    ```c++
    // 1. awaiter.await_ready
    bool awaiter::await_ready() const noexcept {
        // 定义在<seastar/core/preempt.hh>中的need_preempt用来判断是否抢占并将
        // 控制权交还给reactor引擎，原因见后续分析
        // 当不需要抢占且已经就绪时，不会进入协程流程而是直接返回结果
        return _future.available() && !need_preempt();
    }
    
    // 2a. awaiter.await_resume
    // 协程的结果就是从就绪的future中获取
    void awaiter::await_resume() { _future.get(); }

    // 2b. awaiter.await_suspend
    // 返回void的await_suspend总是将执行流交给caller/resumer，这里交给了caller，即reactor::run
    template<typename U>
    void awaiter::await_suspend(SEASTAR_INTERNAL_COROUTINE_NAMESPACE::coroutine_handle<U> hndl) noexcept {
        // 此时由于future已经完成，因此这里_future.available()返回true
        if (!_future.available()) {
            _future.set_coroutine(hndl.promise());
        } else {
            // 调用定义在<seastar/core/task.hh>下的schedule将控制权移交给底层的reactor执行引擎
            // 这里通过coroutine_handle::promise将promise对象的地址传给了reactor
            schedule(&hndl.promise());
        }
    }

    // 3. engine().add_task(t)
    // TODO: reactor引擎的调度执行暂时略过

3. 返回reactor引擎的执行流后，这个future对应的task最终在`reactor::run_tasks(task_queue &tq)`被实际执行：`engine().run() -> run_some_tasks() -> run_tasks`

    ```C++
    void reactor::run_tasks(task_queue& tq) {
        // ...
        auto tsk = tasks.front();
        tasks.pop_front();
        STAP_PROBE(seastar, reactor_run_tasks_single_start);
        task_histogram_add_task(*tsk);
        _current_task = tsk;
        tsk->run_and_dispose(); // <- 任务被执行
        // ...
    }

    // 回顾seastar定义的协程promise类型（coroutine_traits_base::promise_type）
    // 继承了seastar::task并且实现了run_and_dispose接口
    virtual void coroutine_traits_base::promise_type::run_and_dispose() noexcept override {
        // 通过协程的from_promise从promise对象即自身重新获得了协程的handler
        // 通过handle.resume()直接继续协程的运行
        auto handle = SEASTAR_INTERNAL_COROUTINE_NAMESPACE::coroutine_handle<promise_type>::from_promise(*this);
        handle.resume();
    }
    ```

    从执行流程来看，似乎已经ready的`future`如果在`need_preempt() == true`时就会通过reactor引擎来处理，为什么？

    假如有大量的`future`都是就绪状态并且连续被处理，此时在reactor队列中的其他事件、以及其他需要poll的事件就会面临**饥饿**，因此[为了避免饥饿](https://github.com/scylladb/seastar/blob/master/doc/tutorial.md#ready-futures)，当连续执行一定数量的就绪`future`后就会被reactor引擎抢占执行权

#### 2. 当这个`future`对象尚未完成时

例如`seastar::sleep(1s)`

0. 同上
1. 同上
2. 同上，但是会走入`_future.set_coroutine(hndl.promise());`分支，此时`future`还未就绪，到这里控制流就会回到caller，即`reactor::run`

    ```C++
    void internal::future_base::set_coroutine(task& coroutine) noexcept {
        assert(_promise);
        _promise->_task = &coroutine;
    }
    ```

3. 当`future`的条件最终满足时，reactor引擎会调用`promise::set_value`将结果提供给`future`

    ```c++
    template <typename... A>
    void promise_base_with_type::set_value(A&&... a) noexcept {
        if (auto *s = get_state()) {
            s->set(std::forward<A>(a)...);
            make_ready<urgent::no>(); // <- 发起恢复执行协程
        }
    }

    template <promise_base::urgent Urgent>
    void promise_base::make_ready() noexcept {
        if (_task) {
            // 回顾future对象已经完成时的执行流中，执行schedule后的行为
            if (Urgent == urgent::yes) {
                ::seastar::schedule_urgent(std::exchange(_task, nullptr));
            } else {
                ::seastar::schedule(std::exchange(_task, nullptr));
            }
        }
    }
    ```

4. 调度后最终在`reactor::run_tasks`中执行了`task::run_and_dispose -> handle::resume`，协程恢复执行

### `.then()`

seastar中的`future`可以通过`.then(func)`的方式要求在该`future`就绪时将其结果传递给`func`从而实现串联执行的语法，例如：

```C++
seastar::future<int> slow() {
    using namespace std::chrono_literals;
    return seastar::sleep(100ms).then([] { return 3; });
}

seastar::future<> f() {
    return slow().then([] (int val) {
        std::cout << "Got " << val << "\n"; // will output 3
    });
}
```

seastar底层通过reactor引擎来执行tasks，那么`.then()`的串联执行是如何实现的？从源码（略去一些注释和代码）来看：

1. `future::then`在一些包装后，实际调用了`future::then_impl`如下，分当前`future`就绪与否分别处理

    ```c++
    template <typename Func, typename Result = futurize_t<internal::future_result_t<Func, T SEASTAR_ELLIPSIS>>>
    Result future::then_impl(Func&& func) noexcept {
        using futurator = futurize<internal::future_result_t<Func, T SEASTAR_ELLIPSIS>>;

        // 对于已经有结果fail/available的对象，直接处理而不是等待reactor执行
        if (failed()) {
            return futurator::make_exception_future(static_cast<future_state_base&&>(get_available_state_ref()));
        } else if (available()) {
            return futurator::invoke(std::forward<Func>(func), get_available_state_ref().take_value());
        }

        // 对于还未就绪的对象，将下一个调用的函数串联上
        return then_impl_nrvo<Func, Result>(std::forward<Func>(func));
    }
    ```

2. 在`future::then_impl_nrvo`中会根据`.then(func)`的传入`func`其返回类型构造一个新的`future`来返回

    ```C++
    template <typename Func, typename Result>
    Result future::then_impl_nrvo(Func&& func) noexcept {
        // 构造新的future对象
        using futurator = futurize<internal::future_result_t<Func, T SEASTAR_ELLIPSIS>>;
        typename futurator::type fut(future_for_get_promise_marker{});
        using pr_type = decltype(fut.get_promise());

        // schedule中的第二个参数就是当前future就绪时会调用的新函数
        // 而第三个参数是对第二个参数的包装，从而能满足前一个调用出现异常时不在链式调用下一个
        // 第三个lambda作为wrapper，会在第4步的continuation::run_and_dispose()被调用
        schedule(fut.get_promise(), std::move(func), [](pr_type&& pr, Func& func, future_state&& state) {
            if (state.failed()) {
                pr.set_exception(static_cast<future_state_base&&>(std::move(state)));
            } else {
                futurator::satisfy_with_result_of(std::move(pr), [&func, &state] {
                    return internal::future_invoke(func, std::move(state).get_value());
                });
            }
        });
        return fut;
    }
    ```

3. 在`future::schedule`中首先使用`continuation`包装了下一个运行的函数`wrapper`，随后在`future_base::schedule`中将**当前`future`对应的`promise`对象的`_state/_task`更新为下一个函数的包装**，从而能够当前`future`就绪时（即对应的`promise::set_value`被调用）直接调度对应的`continuation` —— **链式关系**

    ```C++
    template <typename Pr, typename Func, typename Wrapper>
    void future::schedule(Pr&& pr, Func&& func, Wrapper&& wrapper) noexcept {
        memory::scoped_critical_alloc_section _;
        auto tws = new continuation<Pr, Func, Wrapper, T SEASTAR_ELLIPSIS>(std::move(pr), std::move(func), std::move(wrapper));
        schedule(tws);
        _state._u.st = future_state_base::state::invalid;
    }

    void future::schedule(continuation_base<T SEASTAR_ELLIPSIS>* tws) noexcept {
        future_base::schedule(tws, &tws->_state);
    }

    void future_base::schedule(task* tws, future_state_base* state) noexcept {
        promise_base* p = detach_promise();
        p->_state = state;
        p->_task = tws;
    }
    ```

4. 与[此处](#2-当这个future对象尚未完成时)中的第3步相同，在链式关系的前一个任务完成时就会执行`.then(func)`传入的函数

    ```c++
    template <typename... A>
    void promise_base_with_type::set_value(A&&... a) noexcept {
        if (auto *s = get_state()) {
            s->set(std::forward<A>(a)...);
            make_ready<urgent::no>(); // <- 发起执行`.then(func)`的func
        }
    }

    template <promise_base::urgent Urgent>
    void promise_base::make_ready() noexcept {
        if (_task) {
            // 回顾future对象已经完成时的执行流中，执行schedule后的行为
            if (Urgent == urgent::yes) {
                ::seastar::schedule_urgent(std::exchange(_task, nullptr));
            } else {
                ::seastar::schedule(std::exchange(_task, nullptr));
            }
        }
    }

    virtual void continuation::run_and_dispose() noexcept override {
        try {
            // 在第3步中对func的包装wrapper此时被调用
            _wrapper(std::move(this->_pr), _func, std::move(this->_state));
        } catch (...) {
            this->_pr.set_to_current_exception();
        }
        // 在第3步中new的continuation在这里被回收
        delete this;
    }
    ```

## 异步执行的任务

以`seastar::sleep`为例（定义在`<seastar/core/sleep.hh>`），可以看出`seastar`是如何异步执行任务的

```c++
template <typename Clock = steady_clock_type, typename Rep, typename Period>
future<> sleep(std::chrono::duration<Rep, Period> dur) {
    struct sleeper {
        promise<> done;
        timer<Clock> tmr;
        sleeper(std::chrono::duration<Rep, Period> dur)
            : tmr([this] { done.set_value(); })
        {
            tmr.arm(dur);
        }
    };
    sleeper *s = new sleeper(dur); // (1)
    future<> fut = s->done.get_future(); // (2)
    return fut.then([s] { delete s; }); // (3)
}
```

异步执行的任务改造成协程的方式非常直接，只需要在原先异步回调中调用`coroutine_handle::resume`就可以恢复协程的执行，在此seastar中所有异步任务都会由reactor引擎来执行

1. 首先构造了`sleeper`对象，对象内的`tmr.arm(dur)`将异步任务（即超时后唤醒）提交给reactor引擎

    ```C++
    template <typename Clock>
    inline
    void timer<Clock>::arm(time_point until, std::optional<duration> period) noexcept {
        arm_state(until, period);
        engine().add_timer(this);
    }
    ```

2. 从对应的`promise`获取`future`对象并返回
3. 当`future`满足时，清理`sleeper`对象