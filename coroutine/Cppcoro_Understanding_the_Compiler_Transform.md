# C++ Coroutines: Understanding the Compiler Transform

[original post](https://lewissbaker.github.io/2022/08/27/understanding-the-compiler-transform)

## Setting the Scene

```cpp
task f(int x);

task g(int x) {
    int fx = co_await f(x);
    co_return fx * fx;
}
```

## Defining the `task` type

```cpp
class task {
public:
    struct awaiter;

    class promise_type {
    public:
        promise_type() noexcept;
        ~promise_type();

        struct final_awaiter {
            bool await_ready() noexcept {
                return false;
            }
            std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> h) noexcept {
                // The coroutine is now suspended at the final-suspend point.
                // Lookup its continuation in the promise and resume it.
                h.promise().continuation.resume();
            }
            void await_resume() noexcept {
            }
        };

        task get_return_object() noexcept {
            return task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_always initial_suspend() noexcept {
            return {};
        }
        final_awaiter final_suspend() noexcept {
            return {};
        }
        void unhandled_exception() noexcept {
            std::terminate();
        }
        void return_value(int result) noexcept {
            result_.emplace(result);
        }

    private:
        friend task::awaiter;
        std::coroutine_handle<> continuation_;
        std::variant<std::monostate, int, std::exception_ptr> result_;
    };

    task(task&& t) noexcept : coro_(std::exchange(t.coro_, {})) {
    }
    ~task() {
        if (coro_) coro_.destroy();
    }
    task& operator=(task&& t) noexcept;

    struct awaiter {
        explicit awaiter(std::coroutine_handle<promise_type> h) noexcept : coro_(h) {
        }
        bool await_ready() noexcept {
            return false;
        }
        std::coroutine_handle<> await_suspend(std::coroutine_handle<> continuation) noexcept {
            // Store the continuation in the task's promise so that the final_suspend()
            // knows to resume this coroutine when the task completes.
            coro_.promise().continuation = continuation;

            // Then we tail-resume the task's coroutine, which is currently suspended
            // at the initial-suspend-point (ie. at the open curly brace), by returning
            // its handle from await_suspend().
            return coro_;
        }
        int await_resume() {
        }
    private:
        std::coroutine_handle<promise_type> coro_;
    };

    awaiter operator co_await() && noexcept {
        return awaiter{coro_};
    }

private:
    explicit task(std::coroutine_handle<promise_type> h) noexcept : coro_(h) {
    }

    std::coroutine_handle<promise_type> coro_;
};
```

## Step 1: Determining the promise type

当编译器遇到`co_await/co_yield/co_return`任一关键词时，就会开始协程改写过程，第一步就是确认该协程的`promise_type`，通过**替换`std::coroutine_traits`模版参数中的返回类型和参数类型**，例如对示例函数`g`可以替换得到该协程的`promise_type`为`std::coroutine_traits<task, int>::promise_type`（后续简单采用`__g_promise_t`来代替）

并且由于我们并没有特化该类型，默认情况下其会解析为返回类型内部定义的`promise_type`，本例中就是`task::promise_type`

## Step 2: Creating the coroutine state

协程需要保存协程的执行状态、参数、本地变量以供后续恢复执行时使用，因此需要创建协程状态，称为**coroutine state**，并且通常是分配在堆上（有一些优化措施可以在安全的情况下移除不必要堆内存分配）

coroutine state会包含：

- **promise对象**
- **函数参数的拷贝**
- **协程的暂停点信息suspend-point，以及恢复resume/摧毁destroy的信息**
- **跨越suspend-point的本地变量/临时对象的存储**

```cpp
struct __g_state {
    int x;
    __g_promise_t __promise;
};
```

编译器会首先尝试采用**左值引用的参数**来构造promise类型，若无法这样构造就会采用promise类型的默认构造，如下：

```cpp
template <typename Promise, typename... Params>
Promise construct_promise([[maybe_unused]] Param&... params) {
    if constexpr (std::constructible_from<Promise, Params&...>) {
        return Promise(params...);
    } else {
        return Promise();
    }
}

struct __g_state {
    __g_state(int&& x)
    : x(static_cast<int&&>(x))
    , __promise(construct_promise<__g_promise_t>(x)) {}

    int x;
    __g_promise_t __promise;
};
```

从而一个协程的改写从分配coroutine state开始，假如从堆分配，则：

```cpp
task g(int x) {
    auto* state = new __g_state(static_cast<int&&>(x));
    // ...
}
```

注意，**coroutine state的分配也是可以定制化的**，假如promise类型有自定义的`operator new`，则会优先选用该分配函数：

而内存分配的函数同样也会传入协程函数的参数，允许可以**根据参数进行不同的内存分配定制化**，这也被称为**parameter preview**

```cpp
template <typename Promise, typename... Args>
void* __promise_allocate(std::size_t size, [[maybe_unused]] Args&... args) {
    if constexpr (requires { Promise::operator new(size, args...); }) {
        return Promise::operator new(size, args...);
    } else {
        return Promise::operator new(size);
    }
}

task g(int x) {
    void* state_mem = __promise_allocate<__g_promise_t>(sizeof(__g_state), x);
    __g_state* state;
    try {
        state = ::new (state_mem) __g_state(static_cast<int&&>(x));
    } catch (...) {
        __g_promise_t::operator delete(state_mem);
        throw;
    }
}
```

假如promise类型还额外定义了**分配失败的处理**，即`get_return_object_on_allocation_failure()`，则编译器会改写为如下流程：

```cpp
task g(int x) {
    auto* state = ::new (std::nothrow) __g_state(static_cast<int&&>(x));
    if (state == nullptr) {
        return __g_promise_t::get_return_object_on_allocation_failure();
    }
}
```

## Step 3: Call `get_return_object()`

```cpp
task g(int x) {
    // get_return_object() might throw
    std::unique_ptr<__g_state> state(new __g_state(static_cast<int&&>(x)));
    decltype(auto) return_value = state->__promise.get_return_object();
    // ...
    return return_value;
}
```

## Step 4: The initial-suspend point

在起始阶段首先是执行`co_await promise.initial_suspend()`，这一步考虑到异常有额外一些细节：

- 当异常从下述调用中抛出时，会直接扩散到上层，coroutine state会被自动销毁
  - `initial_suspend()`
  - `operator co_await()` on the returned awaitable
  - `await_ready()` on the awaiter
  - `await_suspend()` on the awaiter
- 当异常从下述调用中抛出时，异常会被协程捕获并调用`promise.unhandled_exception()`
  - `await_resume()`
  - destructor of object returned from `operator co_await()`
  - destructor of object returned from `initial_suspend()`

由于从`initial_suspend()`和`operator co_await()`返回的对象其生命周期将会跨越suspend-point，因此需要将其也存储在coroutine state上，这里采用辅助类`manual_lifetime`来帮助**手动处理上述返回对象的生命周期问题**

```cpp
template <typename T>
struct manual_lifetime {
    manual_lifetime() noexcept = default;
    ~manual_lifetime() = default;
    // delete all copy/move operations

    template <typename Factory>
    requires std::invocable<Factory&> && std::same_as<std::invoke_result_t<Factory&>, T>
    T& construct_from(Factory factory) noexcept(std::is_nothrow_invocable_v<Factory&>) {
        return *::new (static_cast<void*>(&storage)) T(factory());
    }

    void destroy() noexcept(std::is_nothrow_destructible_v<T>) {
        std::destroy_at(std::launder(reinterpret_cast<T*>(&storage)));
    }

    T& get() & noexcept {
        return *std::launder(reinterpret_cast<T*>(&storage));
    }

    // TODO: notes: for std::launder, see https://www.zhihu.com/question/598256149/answer/3006341876

private:
    alignas(T) std::byte storage[sizeof(T)];
};
```

在本例中，`initial_suspend()`的返回值总是`std::suspend_always`，因此采用`manual_lifetime`来管理该返回对象，而该对象并没有`operator co_await()`，因此省略一个额外的`operator co_await()`返回对象管理，coroutine state如下：

```cpp
struct __g_state {
    __g_state(int&& x);

    int x;
    __g_promise_t _promise;
    manual_lifetime<std::suspend_always> __tmp1;
};
```

当通过`initial_suspend()`构造对象后，就是**调用实现了`co_await`的三个方法**，[参考此](Cppcoro_Understanding_co_await.md)：

- `await_ready()`
- `await_suspend()`
  需要传入当前coroutine的handle，本例中即调用`std::coroutine_handle<__g_promise_t>::from_promise()`，由于本例`await_suspend()`返回对象为`void`因此不需要考虑是否恢复该协程或执行其他协程
- `await_resume()`

由于在`std::suspend_always` **awaiter**上的方法均是不会抛出异常的`noexcept`，因此可以简化该阶段异常的处理，当`await_suspend()`能成功无异常返回时，由`unique_ptr`管理的coroutine state就不应该自动释放，此时应该显式`release`

```cpp
task g(int x) {
    std::unique_ptr<__g_state> state(new __g_state(static_cast<int&&>(x)));
    decltype(auto) __return_val = state->__promise.get_return_object();

    state->__tmp1.construct_from([&]() -> decltype(auto) {
        return state->__promise.initial_suspend();
    });
    if (!state->__tmp1.get().await_ready()) {
        // ... suspend-coroutine here
        state->__tmp1.get().await_suspend(
            std::coroutine_handle<__g_promise_t>::from_promise(state->__promise));
        state.release();
    } else {
        // coroutine did not suspend
        state.release();
        // ... start executing the coroutine body
    }
    return __return_val;
}
```

`await_resume()`以及`__tmp1`的析构若出现异常则会被coroutine捕获由`promise.unhandled_exception()`处理，因此这两个调用会出现在coroutine body中，因此这里不再出现

## Step 5: Recording the suspend-point

协程每一次暂停都需要**记录暂停的位置**，从而在恢复执行时从前一次暂停的位置继续执行，同时也要记录每一个暂停点有效存活的对象，从而当协程被销毁时这些对象也会被正常析构

一种典型的记录方式是在每一个暂停点使用整数索引记录位置（目前的三大主流编译器均采用这种实现）：

```cpp
struct __g_state {
    __g_state(int&& x);

    int x;
    __g_promise_t __promise;
    int __suspend_point = 0;
    manual_lifetime<std::suspend_always> __tmp1;
};
```

## Step 6: Implementing `coroutine_handle::resume()` and `coroutine_handle::destroy()`

恢复协程执行时调用`coroutine_handle::resume()`需要能够根据暂停点找到正确的恢复位置，另外销毁协程时调用`coroutine_handle::destroy()`也需要能够正确的销毁在暂停点存活的对象

`coroutine_handle`的接口对具体的实现类没有任何信息，`coroutine_handle<void>`可以**指向任意协程实例**，因此需要采用**类型擦除type-erasure**的方式，例如采用函数指针的方式，并且在`resume/destroy`时实际调用具体的函数指针：

```cpp
struct __coroutine_state {
    using __resume_fn = void(__coroutine_state*);
    using __destroy_fn = void(__coroutine_state*);

    __resume_fn* __resume;
    __destroy_fn* __destroy;
};

namespace std
{
    template <typename Promise = void>
    class coroutine_handle;

    template <>
    class coroutine_handle<void> {
    public:
        coroutine_handle() noexcept = default;
        // default copy operations

        void* address() const {
            return static_cast<void*>(state_);
        }

        static coroutine_handle from_address(void* ptr) {
            coroutine_handle h;
            h.state_ = static_cast<__coroutine_state*>(ptr);
            return h;
        }

        explicit operator bool() noexcept {
            return state_ != nullptr;
        }

        friend bool operator==(coroutine_handle a, coroutine_handle b) noexcept {
            return a.state_ == b.state_;
        }

        void resume() const {
            state_->__resume(state_);
        }

        void destroy() const {
            state_->__destroy(state_);
        }

        bool done() const {
            return state_->__resume == nullptr;
        }

    private:
        __coroutine_state* state_ = nullptr;
    }
}
```

## Step 7: Implementing `coroutine_handle<Promise>::promise()` and `from_promise()`

同样由于`coroutine_handle<Promise>`需要能够操作任意的promise类型为`Promise`的coroutine state，也需要采用类似的方式，定义coroutine state继承`__coroutine_state`来包含promise对象

```cpp
template <typename Promise>
struct __coroutine_state_with_promise : __coroutine_state {
    __coroutine_state_with_promise() noexcept {}
    ~__coroutine_state_with_promise() {}

    union {
        Promise __promise;
    };
};
```

注意，由于作为基类，其数据构造会先于派生类的构造进行，而我们**需要`Promise`的构造能够在派生类的参数复制之后进行**，从而参数复制可能会被传递给`Promise`的构造，因此将`Promise`放置在`union`中：

```cpp
struct __g_state : __coroutine_state_with_promise<__g_promise_t> {
    __g_state(int&& __x)
    : x(static_cast<int&&>(__x)) {
        // initialize promise in the base class
        ::new ((void*)std::addressof(this->__promise))
            __g_promise_t(construct_promise<__g_promise_t>(x));
    }
    ~__g_state() {
        // manually destruct the promise in the base class
        this->__promise.~__g_promise_t();
    }

    int __suspend_point = 0;
    int x;
    manual_lifetime<std::suspend_always> __tmp1;
};
```

`coroutine_handle<Promise>`其余部分与`coroutine_handle<void>`基本相同，额外多了与`Promise`交互的部分：

- `promise()`
- `from_promise()`

```cpp
namespace std
{
    template<typename Promise>
    class coroutine_handle {
        using state_t = __coroutine_state_with_promise<Promise>;
    public:
        // default ctor/copy operations
        operator coroutine_handle<void>() const noexcept {
            return coroutine_handle<void>::from_address(address());
        }

        Promise& promise() const {
            return state_->__promise;
        }

        static coroutine_handle from_promise(Promise& promise) {
            coroutine_handle h;
            h.state_ = reinterpret_cast<state_t*>(
                reinterpret_cast<unsigned char*>(
                    std::addressof(promise)) - offsetof(state_t, __promise));
            return h;
        }

        // ... basically the same as coroutine_handle<void>
    };
}
```

## Step 8: The beginnings of the coroutine body

```cpp
void __g_resume(__coroutine_state* s);
void __g_destroy(__coroutine_state* s);

struct __g_state : __coroutine_state_with_promise<__g_promise_t> {
    __g_state(int&& __x)
    : x(static_cast<int&&>(__x)) {
        this->__resume = &__g_resume;
        this->__destroy = &__g_destroy;
        ::new ((void*)std::addressof(this->__promise))
            __g_promise_t(construct_promise<__g_promise_t>(x));
    }
    // ... omitted
}

task g(int x) {
    std::unique_ptr<__g_state> state(new __g_state(static_cast<int&&>(x)));
    decltype(auto) return_value = state->__promise.get_return_object();

    state->__tmp1.construct_from([&]() -> decltype(auto) {
        return state->__promise.initial_suspend();
    });
    if (!state->__tmp1.get().await_ready()) {
        state->__tmp1.get().await_suspend(
            std::coroutine_handle<__g_promise_t>::from_promise(state->__promise));
        state.release();
        // fall through to return statement below.
    } else {
        // Coroutine did not suspend. Start executing the body immediately.
        __g_resume(state.release());
    }
    return return_value;
}
```

```cpp
void __g_resume(__coroutine_state* s) {
    // We know that 's' points to a __g_state.
    auto* state = static_cast<__g_state*>(s);

    // Generate a jump-table to jump to the correct place in the code based
    // on the value of the suspend-point index.
    switch (state->__suspend_point) {
    case 0: goto suspend_point_0;
    default: std::unreachable();
    }

suspend_point_0:
    state->__tmp1.get().await_resume();
    state->__tmp1.destroy();

    // TODO: Implement rest of coroutine body.
    //
    //  int fx = co_await f(x);
    //  co_return fx * fx;
}

void __g_destroy(__coroutine_state* s) {
    auto* state = static_cast<__g_state*>(s);

    switch (state->__suspend_point) {
    case 0: goto suspend_point_0;
    default: std::unreachable();
    }

suspend_point_0:
    state->__tmp1.destroy();
    goto destroy_state;

    // TODO: Add extra logic for other suspend-points here.

destroy_state:
    delete state;
}
```

## Step 9: Lowering the `co_await` expression

对于`co_await f(x)`，首先`f(x)`会返回一个临时的`task`对象，该对象跨越了暂停点，因此需要被保存到coroutine state中，同时`co_await task`会调用`operator co_await()`并生成`awaiter`对象，也跨越了暂停点，需要保存到coroutine state中

```cpp
struct __g_state : __coroutine_state_with_promise<__g_promise_t> {
    __g_state(int&& __x);
    ~__g_state();

    int __suspend_point = 0;
    int x;
    manual_lifetime<std::suspend_always> __tmp1;
    manual_lifetime<task> __tmp2;
    manual_lifetime<task::awaiter> __tmp3;
};
```

相应的`__g_resume()`就需要相应的处理第一个暂停点的恢复流程，注意`await_suspend()`如果返回了coroutine handle，则需要调用其`resume()`：

```cpp
void __g_resume(__coroutine_state* s) {
    // We know that 's' points to a __g_state.
    auto* state = static_cast<__g_state*>(s);

    // Generate a jump-table to jump to the correct place in the code based
    // on the value of the suspend-point index.
    switch (state->__suspend_point) {
    case 0: goto suspend_point_0;
    case 1: goto suspend_point_1; // <-- add new jump-table entry
    default: std::unreachable();
    }

suspend_point_0:
    state->__tmp1.get().await_resume();
    state->__tmp1.destroy();

    //  int fx = co_await f(x);
    state->__tmp2.construct_from([&] {
        return f(state->x);
    });
    state->__tmp3.construct_from([&] {
        return static_cast<task&&>(state->__tmp2.get()).operator co_await();
    });
    if (!state->__tmp3.get().await_ready()) {
        // mark the suspend-point
        state->__suspend_point = 1;

        auto h = state->__tmp3.get().await_suspend(
            std::coroutine_handle<__g_promise_t>::from_promise(state->__promise));
        
        // Resume the returned coroutine-handle before returning.
        h.resume();
        return;
    }

suspend_point_1:
    int fx = state->__tmp3.get().await_resume();
    state->__tmp3.destroy();
    state->__tmp2.destroy();

    // TODO: Implement
    //  co_return fx * fx;

void __g_destroy(__coroutine_state* s) {
    auto* state = static_cast<__g_state*>(s);

    switch (state->__suspend_point) {
    case 0: goto suspend_point_0;
    case 1: goto suspend_point_1; // <-- add new jump-table entry
    default: std::unreachable();
    }

suspend_point_0:
    state->__tmp1.destroy();
    goto destroy_state;

suspend_point_1:
    state->__tmp3.destroy();
    state->__tmp2.destroy();
    goto destroy_state;

    // TODO: Add extra logic for other suspend-points here.

destroy_state:
    delete state;
}
}
```

**在这里，`__tmp3.get().await_suspend(...)`是`f(x)`协程对应的`awaiter`（其由`f(x)`返回的`task.operator co_await`所构造），所传入的`std::coroutine_handle`代表了`g(x)`协程（其由`g(x)`写成的`state->__promise`所构造），因此:**

- 若在该`await_suspend(...)`内调用了`.resume()`，那么可以是`f(x)`返回的`task`已经就绪，因此可以直接从`g(x)`的函数体中被暂停的位置继续恢复执行`__g_resume(handle_of_g(x))`
- 该`await_suspend(...)`返回的`h`，其可以是：
  - `f(x)`协程的handle，此时`h.resume()`就代表了开始从`f(x)`上一个暂停点开始继续执行，当`f(x)`执行结束就可以进而调用`g(x)`的`resume`
  - `g(x)`协程的handle，此时说明`f(x)`已经执行完成，此时`h.resume()`就代表了开始从`g(x)`上一个暂停点开始继续执行
  - 也可以是`std::noop_coroutine`，那么在`await_suspend(...)`中需要通过其他方式存储、执行、恢复`f(x)/g(x)`的协程，参考下述Seastar的做法
- **在[Seastar](../seastar/Coroutines.md)中**，底层的Reactor引擎会不断的执行任务，并且任务以**continuation chain**的方式串联起来，从而适配C++20 Coroutine的方式更为简洁
  - Reactor引擎不断的在continuation chain上连续执行任务，直到需要让渡/preemption或是future not ready
  - `seastar::future::operator co_await`返回的`awaiter`就是对`seastar::future`的简单包装
    - `await_ready()`只需要**检查future readiness**（以及preemption）
    - `await_suspend()`只需要将传入的**上一级调用的任务串联到当前任务的末尾**（即当前任务完成时就由Reactor引擎自动开始执行该上一级调用的任务）
    - `await_resume()`即**从future中将结果取出返回**
  - **Seastar的`await_suspend()`采用了旧的接口**，并不会返回`coroutine_handle`，因此生成代码就不会包含`h.resume()`，见`TODO: Understanding Symmetric Transfer`

## Step 10: Implementing `unhandled_exception()`

由于`f(x)`和`awaiter::await_resume()`有可能会抛出异常（没有标记`noexcept`），因此需要协程捕获该异常，并通过`promise.unhandled_exception()`进行扩散

特别注意对`awaiter::await_suspend()`返回的coroutine handle调用`resume()`时可能会抛出的异常不需要被当前协程捕获，因此直接继续抛出，另外这里额外定义了RAII的`destructor_guard`来帮助处理`__tmp3.get().await_resume()`可能抛出异常导致`__tmp3`没有合理析构的问题

```cpp
void __g_resume(__coroutine_state* s) {
    auto* state = static_cast<__g_state*>(s);

    std::coroutine_handle<void> coro_to_resume;

    try {
        switch (state->__suspend_point) {
        case 0: goto suspend_point_0;
        case 1: goto suspend_point_1; // <-- add new jump-table entry
        default: std::unreachable();
        }

suspend_point_0:
        {
            destructor_guard tmp1_dtor{state->__tmp1};
            state->__tmp1.get().await_resume();
        }

        //  int fx = co_await f(x);
        {
            state->__tmp2.construct_from([&] {
                return f(state->x);
            });
            destructor_guard tmp2_dtor{state->__tmp2};

            state->__tmp3.construct_from([&] {
                return static_cast<task&&>(state->__tmp2.get()).operator co_await();
            });
            destructor_guard tmp3_dtor{state->__tmp3};

            if (!state->__tmp3.get().await_ready()) {
                state->__suspend_point = 1;

                coro_to_resume = state->__tmp3.get().await_suspend(
                    std::coroutine_handle<__g_promise_t>::from_promise(state->__promise));

                // A coroutine suspends without exiting scopes.
                // So cancel the destructor-guards.
                tmp3_dtor.cancel();
                tmp2_dtor.cancel();

                goto resume_coro;
            }

            // Don't exit the scope here.
            //
            // We can't 'goto' a label that enters the scope of a variable with a
            // non-trivial destructor. So we have to exit the scope of the destructor
            // guards here without calling the destructors and then recreate them after
            // the `suspend_point_1` label.
            tmp3_dtor.cancel();
            tmp2_dtor.cancel();
        }

suspend_point_1:
        int fx = [&]() -> decltype(auto) {
            destructor_guard tmp2_dtor{state->__tmp2};
            destructor_guard tmp3_dtor{state->__tmp3};
            return state->__tmp3.get().await_resume();
        }();

        // TODO: Implement
        //  co_return fx * fx;
    } catch (...) {
        state->__promise.unhandled_exception();
        goto final_suspend;
    }

final_suspend:
    // TODO: Implement
    // co_await promise.final_suspend();

resume_coro:
    coro_to_resume.resume();
    return;
}
```

*略去`__promise.unhandled_excetion()`本身会抛出异常的特殊处理，见原文*

## Step 11: Implementing `co_return`

`co_return`会被直接替换为：

```cpp
// promise.return_value(<expr>);
// goto final-suspend-point;

state->__promise.return_value(fx * fx);
goto final_suspend;
```

## Step 12: Implementing `final_suspend()`

`final_suspend()`方法需要返回一个临时的`task::promise_type::final_awaiter`对象，该对象也需要存储到coroutine state中，且被`__g_destroy()`正确销毁，并且该对象并没有`operator co_await`，因此不需要处理相应的`awaiter`

```cpp
struct __g_state : __coroutine_state_with_promise<__g_promise_t> {
    __g_state(int&& __x);
    ~__g_state();

    int __suspend_point = 0;
    int x;
    manual_lifetime<std::suspend_always> __tmp1;
    manual_lifetime<task> __tmp2;
    manual_lifetime<task::awaiter> __tmp3;
    manual_lifetime<task::promise_type::final_awaiter> __tmp4; // <---
};
```

略去`__g_destroy()`，其与前述的修改类似，而相应的`__g_resume()`中的`final_suspend`部分就是:

```cpp
final_suspend:
    // co_await promise.final_suspend
    {
        state->__tmp4.construct_from([&]() noexcept {
            return state->__promise.final_suspend();
        });
        destructor_guard tmp4_dtor{state->__tmp4};

        if (!state->__tmp4.get().await_ready()) {
            state->__suspend_point = 2;
            state->__resume = nullptr; // mark as final suspend-point

            coro_to_resume = state->__tmp4.get().await_suspend(
                std::coroutine_handle<__g_promise_t>::from_promise(state->__promise));

            tmp4_dtor.cancel();
            goto resume_coro;
        }

        state->__tmp4.get().await_resume();
    }

    //  Destroy coroutine-state if execution flows off end of coroutine
    delete state;
    return;
```

## Step 13: Implementing symmetric-transfer and the noop-coroutine

前述的`__g_resume()`会面临StackOverflow的问题，见`TODO: Understanding Symmetric Transfer`

**采用tail call的方式将递归优化为循环**：

```cpp
struct __coroutine_state {
    using __resume_fn = __coroutine_state* (__coroutine_state*);
    using __destroy_fn = void (__coroutine_state*);

    __resume_fn* __resume;
    __destroy_fn* __destroy;
};

void std::coroutine_handle<void>::resume() const {
    __coroutine_state* s = state_;
    do {
        s = s->__resume(s);
    } while (s != &__coroutine_state::__noop_coroutine);
}
```

从而前述的`__g_resume()`的实现也可以修改为返回`coroutine_handle`：

```cpp
coro_to_resume = ...;
goto resume_coro;

// replaced with

auto h = ...;
return static_cast<__coroutine_state*>(h.address());

// before delete state to stop the while loop
return static_cast<__coroutine_state*>(std::noop_coroutine().address());
```

## One last thing

在每个suspend point所用到的存储变量不尽相同，即**临时对象的生命周期不同**，此时可以通过**复用临时变量的存储空间**来优化coroutine state的占用内存：

- `__tmp1`: 仅在`co_await promise.initial_suspend()`中生存
- `__tmp2`: 仅在`int fx = co_await f(x)`中生存
- `__tmp3`: 仅在`int fx = co_await f(x)`中生存
- `__tmp4`: 仅在`co_await promise.final_suspend()`中生存

从而利用不重叠的临时对象生命周期，coroutine state的临时对象存储可以优化为：

```cpp
struct __g_state : __coroutine_state_with_promise<__g_promise_t> {
    __g_state(int&& x);
    ~__g_state();

    int __suspend_point = 0;
    int x;

    struct __scope1 {
        manual_lifetime<task> __tmp2;
        manual_lifetime<task::awaiter> __tmp3;
    };

    union {
        manual_lifetime<std::suspend_always> __tmp1;
        __scope1 __s1;
        manual_lifetime<task::promise_type::final_awaiter> __tmp4;
    };
};
```

## Tying it all together

```cpp
task g(int x) {
    int fx = co_await f(x);
    co_return fx * fx;
}


/////
// The coroutine promise-type

using __g_promise_t = std::coroutine_traits<task, int>::promise_type;

__coroutine_state* __g_resume(__coroutine_state* s);
void __g_destroy(__coroutine_state* s);

/////
// The coroutine-state definition

struct __g_state : __coroutine_state_with_promise<__g_promise_t> {
    __g_state(int&& x)
    : x(static_cast<int&&>(x)) {
        // Initialise the function-pointers used by coroutine_handle methods.
        this->__resume = &__g_resume;
        this->__destroy = &__g_destroy;

        // Use placement-new to initialise the promise object in the base-class
        // after we've initialised the argument copies.
        ::new ((void*)std::addressof(this->__promise))
            __g_promise_t(construct_promise<__g_promise_t>(this->x));
    }

    ~__g_state() {
        this->__promise.~__g_promise_t();
    }

    int __suspend_point = 0;

    // Argument copies
    int x;

    // Local variables/temporaries
    struct __scope1 {
        manual_lifetime<task> __tmp2;
        manual_lifetime<task::awaiter> __tmp3;
    };

    union {
        manual_lifetime<std::suspend_always> __tmp1;
        __scope1 __s1;
        manual_lifetime<task::promise_type::final_awaiter> __tmp4;
    };
};

/////
// The "ramp" function

task g(int x) {
    std::unique_ptr<__g_state> state(new __g_state(static_cast<int&&>(x)));
    decltype(auto) return_value = state->__promise.get_return_object();

    state->__tmp1.construct_from([&]() -> decltype(auto) {
        return state->__promise.initial_suspend();
    });
    if (!state->__tmp1.get().await_ready()) {
        state->__tmp1.get().await_suspend(
            std::coroutine_handle<__g_promise_t>::from_promise(state->__promise));
        state.release();
        // fall through to return statement below.
    } else {
        // Coroutine did not suspend. Start executing the body immediately.
        __g_resume(state.release());
    }
    return return_value;
}

/////
//  The "resume" function

__coroutine_state* __g_resume(__coroutine_state* s) {
    auto* state = static_cast<__g_state*>(s);

    try {
        switch (state->__suspend_point) {
        case 0: goto suspend_point_0;
        case 1: goto suspend_point_1; // <-- add new jump-table entry
        default: std::unreachable();
        }

suspend_point_0:
        {
            destructor_guard tmp1_dtor{state->__tmp1};
            state->__tmp1.get().await_resume();
        }

        //  int fx = co_await f(x);
        {
            state->__s1.__tmp2.construct_from([&] {
                return f(state->x);
            });
            destructor_guard tmp2_dtor{state->__s1.__tmp2};

            state->__s1.__tmp3.construct_from([&] {
                return static_cast<task&&>(state->__s1.__tmp2.get()).operator co_await();
            });
            destructor_guard tmp3_dtor{state->__s1.__tmp3};

            if (!state->__s1.__tmp3.get().await_ready()) {
                state->__suspend_point = 1;

                auto h = state->__s1.__tmp3.get().await_suspend(
                    std::coroutine_handle<__g_promise_t>::from_promise(state->__promise));

                // A coroutine suspends without exiting scopes.
                // So cancel the destructor-guards.
                tmp3_dtor.cancel();
                tmp2_dtor.cancel();

                return static_cast<__coroutine_state*>(h.address());
            }

            // Don't exit the scope here.
            // We can't 'goto' a label that enters the scope of a variable with a
            // non-trivial destructor. So we have to exit the scope of the destructor
            // guards here without calling the destructors and then recreate them after
            // the `suspend_point_1` label.
            tmp3_dtor.cancel();
            tmp2_dtor.cancel();
        }

suspend_point_1:
        int fx = [&]() -> decltype(auto) {
            destructor_guard tmp2_dtor{state->__s1.__tmp2};
            destructor_guard tmp3_dtor{state->__s1.__tmp3};
            return state->__s1.__tmp3.get().await_resume();
        }();

        //  co_return fx * fx;
        state->__promise.return_value(fx * fx);
        goto final_suspend;
    } catch (...) {
        state->__promise.unhandled_exception();
        goto final_suspend;
    }

final_suspend:
    // co_await promise.final_suspend
    {
        state->__tmp4.construct_from([&]() noexcept {
            return state->__promise.final_suspend();
        });
        destructor_guard tmp4_dtor{state->__tmp4};

        if (!state->__tmp4.get().await_ready()) {
            state->__suspend_point = 2;
            state->__resume = nullptr; // mark as final suspend-point

            auto h = state->__tmp4.get().await_suspend(
                std::coroutine_handle<__g_promise_t>::from_promise(state->__promise));

            tmp4_dtor.cancel();
            return static_cast<__coroutine_state*>(h.address());
        }

        state->__tmp4.get().await_resume();
    }

    //  Destroy coroutine-state if execution flows off end of coroutine
    delete state;

    return static_cast<__coroutine_state*>(std::noop_coroutine().address());
}

/////
// The "destroy" function

void __g_destroy(__coroutine_state* s) {
    auto* state = static_cast<__g_state*>(s);

    switch (state->__suspend_point) {
    case 0: goto suspend_point_0;
    case 1: goto suspend_point_1;
    case 2: goto suspend_point_2;
    default: std::unreachable();
    }

suspend_point_0:
    state->__tmp1.destroy();
    goto destroy_state;

suspend_point_1:
    state->__s1.__tmp3.destroy();
    state->__s1.__tmp2.destroy();
    goto destroy_state;

suspend_point_2:
    state->__tmp4.destroy();
    goto destroy_state;

destroy_state:
    delete state;
}
```
