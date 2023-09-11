# seastar中的协程

## 在seastar中使用协程

C++ 20中引入了协程的语言支持，协程相关内容可以[见此](https://github.com/JasonYuchen/notes/tree/master/coroutine)

seastar作为一个高性能异步编程框架，天然适合通过协程来使用

> seastar可以通过gcc和clang编译并使用协程，要求clang >= 10，而gcc暂时存在[sanitizer不支持协程](https://gcc.gnu.org/bugzilla/show_bug.cgi?id=95137)的问题（使用AddressSanitizer运行时会出现段错误）

示例使用如下，摘自[seastar教程](https://github.com/scylladb/seastar/blob/master/doc/tutorial.md#coroutines)：

```cpp
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

    ```cpp
    template<typename... T>
    auto operator co_await(future<T...> f) noexcept {
        return internal::awaiter<T...>(std::move(f));
    }
    ```

2. 随后就会由编译器转换为[这种执行流](https://github.com/JasonYuchen/notes/blob/master/coroutine/Cppcoro_Understanding_co_await.md#2-awaiting-the-awaiter)

    ```cpp
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
    // reactor引擎的调度执行见此分析
    // https://github.com/JasonYuchen/notes/blob/master/seastar/Reactor.md#class-reactor
    ```

3. 返回[reactor引擎的执行流](https://github.com/JasonYuchen/notes/blob/master/seastar/Reactor.md#class-reactor)后，这个future对应的task最终在`reactor::run_tasks(task_queue &tq)`被实际执行：`engine().run() -> run_some_tasks() -> run_tasks`

    ```cpp
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

    假如有大量的`future`都是就绪状态并且连续被处理，此时在reactor队列中的其他事件、以及其他需要poll的事件就会面临**饥饿**，因此[为了避免饥饿](https://github.com/scylladb/seastar/blob/master/doc/tutorial.md#ready-futures)，当连续执行一定数量（当前默认为256个）的就绪`future`后就会被reactor引擎抢占执行权

#### 2. 当这个`future`对象尚未完成时

例如`seastar::sleep(1s)`

0. 同上
1. 同上
2. 同上，但是会走入`_future.set_coroutine(hndl.promise());`分支，此时`future`还未就绪，到这里控制流就会回到caller，即`reactor::run`

    ```cpp
    void internal::future_base::set_coroutine(task& coroutine) noexcept {
        assert(_promise);
        _promise->_task = &coroutine;
    }
    ```

3. 当`future`的条件最终满足时，reactor引擎会调用`promise::set_value`将结果提供给`future`

    ```cpp
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

### `all()`

只采用`co_await`单次只能等待一个协程，并且若有多个操作时就需要顺序依次`co_await`，限制了一定的并发性（例如**当需要执行多个I/O请求时，顺序依次等待每一个I/O操作的吞吐量不如一次性等待多个I/O操作，这些I/O操作就有更大的机会被批量执行**）seastar允许一次等待多个协程执行完成（将协程转换成多个"子协程"即**seastar fibers**）：

```cpp
seastar::future<int> parallel_sum(int key1, int key2) {
    int [a, b] = co_await seastar::coroutine::all(
        [&] { return read(key1); },
        [&] { return read(key2); });
    co_return a + b;
}
```

**注意`all`会等待所有子任务执行结束，即使某些抛出了异常**，也会等到所有结束后才将异常向上层抛出，当多个异常一起发生时会选择其中任意一个异常，其实现原理也比较直接，核心点在于：

- `await_ready`的判断就是是否所有`future`都已经就绪，若非则第0个开始按顺序`process`
- 处理过程中每一个未就绪的任务都会通过内部类`intermediate_task`进行等待，一旦**任务完成通过回调继续处理下一个任务**
- 按`all`的构造顺序返回所有结果，并跳过其中返回值是`void`的任务，但是对于存在异常的情况，"随机"返回一个异常（实际实现是最后一个异常）

```cpp
/// Wait for serveral futures to complete in a coroutine.
///
/// `all` can be used to launch several computations concurrently
/// and wait for all of them to complete. Computations are provided
/// as callable objects (typically lambda coroutines) that are invoked
/// by `all`. Waiting is performend by `co_await` and returns a tuple
/// of values, one for each non-void future.
///
/// If one or more of the function objects throws an exception, or if one
/// or more of the futures resolves to an exception, then the exception is
/// thrown. All of the futures are waited for, even in the case of exceptions.
/// If more than one exception is present, an arbitrary one is thrown.
template <typename... Futures>
class [[nodiscard("must co_await an all() object")]] all {
    using tuple = std::tuple<Futures...>;
    using value_tuple = typename internal::value_tuple_for_non_void_futures<Futures...>;
    struct awaiter;
    template <size_t idx>
    struct intermediate_task final : continuation_base_from_future_t<std::tuple_element_t<idx, tuple>> {
        awaiter& container;
        explicit intermediate_task(awaiter& container) : container(container) {}
        virtual void run_and_dispose() noexcept {
            using value_type = typename std::tuple_element_t<idx, tuple>::value_type;
            if (__builtin_expect(this->_state.failed(), false)) {
                // 若第idx个任务失败，将相应的future设置异常，但并不会终止所有任务的执行
                using futurator = futurize<std::tuple_element_t<idx, tuple>>;
                std::get<idx>(container.state._futures) = futurator::make_exception_future(std::move(this->_state).get_exception());
            } else {
                // 若执行成功第idx个任务，则将相应的第idx个future设置为执行结果
                if constexpr (std::same_as<std::tuple_element_t<idx, tuple>, future<>>) {
                    std::get<idx>(container.state._futures) = make_ready_future<>();
                } else {
                    std::get<idx>(container.state._futures) = make_ready_future<value_type>(std::move(this->_state).get0());
                }
            }
            // 注意：intermediate_task是通过placement new创建在all类内部的，因此需要手动调用其析构函数
            this->~intermediate_task();
            // 无论第idx个任务是否成功，都会继续执行下一个任务直到all的所有futures就绪
            container.template process<idx+1>();
        }
    };
    template <typename IndexSequence>
    struct generate_aligned_union;
    template <size_t... idx>
    struct generate_aligned_union<std::integer_sequence<size_t, idx...>> {
        // 对每一个任务的future相应的intermedaite_task都对齐的类型continuation_storage_t
        // 采用这种对齐的目的就是提供一个对齐后尚未初始化的内存块，从而所有内含的类型都可以直接在其上进行构造，见cppreference std::aligned_union
        // 随后就用该栈变量_continuation_storage作为临时存储（利用了placement new）来存放所有intermediate_task
        using type = std::aligned_union_t<1, intermediate_task<idx>...>;
    };
    // std::tuple_size_v<tuple> 返回tuple的元素数量
    // std::make_index_sequence<N> 返回 <0, 1, 2, ..., N-1>
    using continuation_storage_t = typename generate_aligned_union<std::make_index_sequence<std::tuple_size_v<tuple>>>::type;
    using coroutine_handle_t = SEASTAR_INTERNAL_COROUTINE_NAMESPACE::coroutine_handle<void>;
private:
    tuple _futures;
private:
    struct awaiter {
        all& state;
        continuation_storage_t _continuation_storage;
        coroutine_handle_t when_ready;
        awaiter(all& state) : state(state) {}
        bool await_ready() const {
            return std::apply([] (const Futures&... futures) {
                return (... && futures.available());
            }, state._futures);
        }
        void await_suspend(coroutine_handle_t h) {
            when_ready = h;
            process<0>();
        }
        value_tuple await_resume() {
            // 在resume之后就会调用await_resume来构造co_await的返回值，对所有就绪的future
            // 进行判断是否存在异常，多个异常时实际实现中会返回all顺序的最后一个异常
            std::apply([] (Futures&... futures) {
                std::exception_ptr e;
                // Call get_exception for every failed future, to avoid exceptional future
                // ignored warnings. 
                (void)(..., (futures.failed() ? (e = futures.get_exception(), 0) : 0));
                if (e) {
                    std::rethrow_exception(std::move(e));
                }
            }, state._futures);
            // This immediately-invoked lambda is used to materialize the indexes
            // of non-void futures in the tuple.
            return [&] <size_t... Idx> (std::integer_sequence<size_t, Idx...>) {
                return value_tuple(std::get<Idx>(state._futures).get0()...);
            } (internal::index_sequence_for_non_void_futures<Futures...>());
        }
        template <unsigned idx>
        void process() {
            // 当不断处理完成末尾任务时就会直接调用resume恢复协程的执行
            if constexpr (idx == sizeof...(Futures)) {
                when_ready.resume();
            } else {
                if (!std::get<idx>(state._futures).available()) {
                    auto task = new (&_continuation_storage) intermediate_task<idx>(*this);
                    // set_callback时会调用schedule()进行该intermediate_task的执行，从而执行到
                    // 上面代码中的run_and_dispose()
                    seastar::internal::set_callback(std::get<idx>(state._futures), task);
                } else {
                    // 若当前task就绪则直接判断下一个task
                    process<idx + 1>();
                }
            }
        }
    };
public:
    template <typename... Func>
    requires (... && std::invocable<Func>) && (... && future_type<std::invoke_result_t<Func>>)
    explicit all(Func&&... funcs)
            : _futures(futurize_invoke(funcs)...) {
    }
    awaiter operator co_await() { return awaiter{*this}; }
};
```

### `when_any()`

`TODO: any()用法与实现`

### `maybe_yield()`

显然每一个`co_await`点都是reactor引擎调度的点，上述也提到了即使一个`future`已经就绪，但如果此前已经连续运行了过多就绪任务（默认阈值为256）那么为了避免其他poller下的任务饥饿，reactor引擎会通过`need_preempt()`返回`true`来抢占任务

如果某个计算密集的任务中并不包含`co_await`点，就可能导致reactor引擎无法通过抢占的方式来避免饥饿，此时就**需要调用者主动调用`co_await maybe_yield();`来检查是否需要让渡出执行权**，需要注意的是这个过程中也发生了coroutine的各种判断，因此避免在一个计算开销并不高的多次循环中调用，而是在计算开销较高且时间长的循环中使用，例如：

```cpp
seastar::future<int> long_loop(int n) {
    float acc = 0;
    for (int i = 0; i < n; ++i) {  // large n
        acc += std::sin(float(i)); // heavy computation
        co_await seastar::coroutine::maybe_yield();
    }
    co_return acc;
}
```

从上述分析可知，`maybe_yield()`的实现非常简单，**只需要在`await_ready()`内部判断是否需要被抢占即可**，其他逻辑与常规`task`类似，如下：

```cpp
struct maybe_yield_awaiter final : task {
    using coroutine_handle_t = SEASTAR_INTERNAL_COROUTINE_NAMESPACE::coroutine_handle<void>;

    coroutine_handle_t when_ready;
    task* main_coroutine_task;

    bool await_ready() const {
        // 主动判断是否需要被抢占
        return !need_preempt();
    }

    template <typename T>
    void await_suspend(SEASTAR_INTERNAL_COROUTINE_NAMESPACE::coroutine_handle<T> h) {
        when_ready = h;
        main_coroutine_task = &h.promise(); // for waiting_task()
        // 显然这个future总是就绪的，并不需要像上文中的task中一样判断
        // 因此直接调度返回给reactor引擎等待被resume
        schedule(this);
    }

    void await_resume() {}

    virtual void run_and_dispose() noexcept override {
        when_ready.resume();
        // No need to delete, this is allocated on the coroutine frame
    }
    virtual task* waiting_task() noexcept override {
        return main_coroutine_task;
    }
};
```

### `with_timeout`

对任务超时的限制实际上**并不会在超时时取消任务直接返回，而是在任务完成时检查任务的完成时间是否超时**，如果超时则将相应的`future`设置为超时异常，其应该等同于`co_await task`结果后判断时间差，若超时就返回`timeout exception`，否则就返回`co_await task`的结果

```cpp
// Wait for either a future, or a timeout, whichever comes first
// Note that timing out doesn't cancel any tasks associated with the original future.
template<typename ExceptionFactory = default_timeout_exception_factory, typename Clock, typename Duration, typename... T>
future<T...> with_timeout(std::chrono::time_point<Clock, Duration> timeout, future<T...> f) {
    if (f.available()) {
        return f;
    }
    auto pr = std::make_unique<promise<T...>>();
    auto result = pr->get_future();
    // 加入一个计时器，当计时结束（超时发生）就给返回的结果设置为超时异常
    timer<Clock> timer([&pr = *pr] {
        pr.set_exception(std::make_exception_ptr(ExceptionFactory::timeout()));
    });
    timer.arm(timeout);
    // Future is returned indirectly.
    // 将任务f后链上一个判断是否发生超时的任务
    (void)f.then_wrapped([pr = std::move(pr), timer = std::move(timer)] (auto&& f) mutable {
        if (timer.cancel()) {
            // 若尚未超时，即timer处于armd状态且未激发，则此时cancel成功返回true，将任务原先的结果推送给
            // with_timeout返回的future
            f.forward_to(std::move(*pr));
        } else {
            // cancel失败说明超时已经发生，则此时忽略原任务的任何结果，此时with_timeout返回的future就处于
            // 被timer的callback设置了超时异常（timeout exception）的状态，caller随后就会发现超时
            f.ignore_ready_future();
        }
    });
    return result;
}
```

### `.then()`

seastar中的`future`可以通过`.then(func)`的方式要求在该`future`就绪时将其结果传递给`func`从而实现串联执行的语法，注意**更推荐采用协程的写法而非`.then()`，协程有更多优越性**，例如：

```cpp
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

    ```cpp
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

    ```cpp
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

    ```cpp
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

    ```cpp
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

```cpp
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

    ```cpp
    template <typename Clock>
    inline
    void timer<Clock>::arm(time_point until, std::optional<duration> period) noexcept {
        arm_state(until, period);
        engine().add_timer(this);
    }
    ```

2. 从对应的`promise`获取`future`对象并返回
3. 当`future`满足时，清理`sleeper`对象

## 协程中的异常处理

**协程会自动捕获异常并放入到返回的`future`中**，当`co_await`的函数抛出异常时，协程也会直接将异常继续向上抛出：

```cpp
seastar::future<> function_returning_an_exceptional_future();

seastar::future<> exception_handling() {
    try {
        co_await function_returning_an_exceptional_future();
    } catch (...) {
        // exception will be handled here
    }
    throw 3; // will be captured by coroutine and returned as
             // an exceptional future
}
```

对于返回的泛型非空时，即`future<T>`而非`future<>`时，传递异常更**推荐使用以下的返回异常而不使用抛出异常**`throw`（受限于编译器，`future<>`不支持这种做法）：

```cpp
seastar::future<int> exception_propagating() {
    std::exception_ptr eptr;
    try {
        co_await function_returning_an_exceptional_future();
    } catch (...) {
        eptr = std::current_exception();
    }
    if (eptr) {
        co_return seastar::coroutine::exception(eptr); // Saved exception pointer can be propagated without rethrowing
    }
    co_return seastar::coroutine::make_exception(3); // Custom exceptions can be propagated without throwing
}
```

**采用`seastar::defer()`（RAII的方式，参考golang的`defer`）来确保异常情况下也能执行一些清理逻辑**
