# C++ Coroutines: Understanding operator co_await

[original post](https://lewissbaker.github.io/2017/11/17/understanding-operator-co-await)

## Compiler <-> Library interaction

Coroutines TS没有定义coroutine的行为，而实际上定义了一个通用的机制让用户可以通过实现一系列方法来自定义coroutine的行为

Coroutines定义了以下两种接口：

- **Promise接口**
    用于定义coroutine的行为自身，即当coroutine被调用到的时候以及返回的时候的行为
- **Awaitable接口**
    用于定义控制`co_await`行为的methods，当调用`co_await`时将会将调用翻译成一系列Awaitable对象的方法，例如是否需要暂停当前的协程，暂停执行后额外的逻辑，恢复执行前额外的逻辑等

## Awaiters and Awaitables: Explaining `operator co_await`

采用了`co_await`的函数就会被编译器识别为coroutine，支持`co_await`的类型称为Awaitable类型，由于定义了`await_transform`方法的Promise类型可以改变`co_await`的含义，因此分为Normally Awaitable（没有`await_transform`）和Contextually Awaitable（定义了`await_transform`）

实现了`await_ready, await_suspend, await_resume`方法的类型称为Awaiter

### 1. Obtaining the Awaiter

假定`P`为**awaiting coroutine**的promise对象的类型，而`promise`为**current coroutine**的promise对象的左值引用

若`P`有成员函数`await_transform`（[参考此处](Cppcoro_Understanding_Promise.md#12-customising-the-behaviour-of-co_await)），则编译器会调用`promise.await_transform(<expr>)`来获得Awaitable的值`awaitable`，否则就会直接使用`<expr>`作为`awaitable`

当该`awaitable`有可用的`operator co_await()`重载时，就会调用并获得Awaiter对象，否则`awaitable`就会被直接作为Awaiter

```cpp
template<typename P, typename T>
decltype(auto) get_awaitable(P& promise, T&& expr)
{
  if constexpr (has_any_await_transform_member_v<P>)
    return promise.await_transform(static_cast<T&&>(expr));
  else
    return static_cast<T&&>(expr);
}

template<typename Awaitable>
decltype(auto) get_awaiter(Awaitable&& awaitable)
{
  if constexpr (has_member_operator_co_await_v<Awaitable>)
    return static_cast<Awaitable&&>(awaitable).operator co_await();
  else if constexpr (has_non_member_operator_co_await_v<Awaitable&&>)
    return operator co_await(static_cast<Awaitable&&>(awaitable));
  else
    return static_cast<Awaitable&&>(awaitable);
}
```

### 2. Awaiting the Awaiter

`co_await <expr>`就会被编译器转换为如下代码：

```cpp
{
  auto&& value = <expr>;
  auto&& awaitable = get_awaitable(promise, static_cast<decltype(value)>(value));
  auto&& awaiter = get_awaiter(static_cast<decltype(awaitable)>(awaitable));

  // await_ready允许在已知操作会同步完成时，不需要进入协程而和常规函数一样执行，从而避免<suspend-coroutine>的开销
  if (!awaiter.await_ready()) {
    using handle_t = std::experimental::coroutine_handle<P>;
    using await_suspend_result_t = decltype(awaiter.await_suspend(handle_t::from_promise(p)));

    // 编译器在此会生成代码保存当前协程状态并为恢复做准备，例如保存<resume-point>的地址、保存寄存器值到协程帧
    <suspend-coroutine>
    // 运行到此<suspend-coroutine>结束，即认为当前协程已被suspended，随后可以resume/destroy

    // await_suspend负责调度，进行协程的resume/destroy，若直接返回false则当前协程会立即resume继续执行

    // 返回void的await_suspend会无条件将控制流传递给caller/resumer
    if constexpr (std::is_void_v<await_suspend_result_t>) {
      awaiter.await_suspend(handle_t::from_promise(p));
      <return-to-caller-or-resumer>
    } else {
      static_assert( std::is_same_v<await_suspend_result_t, bool>,
         "await_suspend() must return 'void' or 'bool'.");
    
      // 返回bool的await_suspend则可以控制是否需要交给caller/resumer
      // 例如awaiter发起的操作已经同步结束了，此时无需suspend而可以直接resume，就不需要交给caller/resumer
      if (awaiter.await_suspend(handle_t::from_promise(p))) {
        <return-to-caller-or-resumer>
      }
    }

    // 当协程最终被resume时，就会在此继续执行
    <resume-point>
  }

  // await_resume()的返回结果就是co_await的返回值
  return awaiter.await_resume();
}
```

注意：**当`await_suspend()`抛出异常时，coroutine会被自动resumed，并且异常会自动抛出`co_await`且不会调用`await_resume()`**

## Coroutine Handles

调用`await_suspend()`时会传入`coroutine_handle<P>`，通过调用此handler从而控制协程的行为，例如`resume()`和`destroy()`：

```cpp
template<typename Promise>
struct coroutine_handle;

// 类型被抹除的coroutine_handle，允许代表任意的coroutine，但无法访问对应的promise对象
template<>
struct coroutine_handle<void>
{
  // 构造空的handle
  constexpr coroutine_handle();

  // 判断是否为空的handle
  constexpr explicit operator bool() const noexcept;

  // 判断是否在final_suspend点已经suspended
  // 对于当前未suspended的协程调用是未定义行为
  bool done() const;

  // 立即恢复协程执行到遇到<return-to-callder-or-resumer>点再返回
  void resume();

  // 析构协程帧并回收所有相关的资源，通常不应该调用
  void destroy();

  // 允许coroutine_handle与void*之间的转换，从而可以作为一个context传递给C-style API
  constexpr void* address() const;
  static constexpr coroutine_handle from_address(void* address);
};

// 对于绝大部分Normally Awaitable类型来说，应该使用coroutine_handle<void>，带有已知的promise类型
template<typename Promise>
struct coroutine_handle : coroutine_handle<void>
{
  using coroutine_handle<>::coroutine_handle;

  // 返回协程的promise对象的引用，通常不应该调用
  Promise& promise() const;

  // 通过promise对象来重建coroutine_handle
  static coroutine_handle from_promise(Promise& promise);

  static constexpr coroutine_handle from_address(void* address);
};
```

## Synchronisation-free async code

从[Awaiting the Awaiter](#2-awaiting-the-awaiter)可以看出，在当前coroutine被suspend后，而在resume前，`await_suspend()`中可以执行任意自定义的逻辑（从而可以在这里实现coroutines的调度切换）

可以在coroutine被suspend后，在`await_suspend()`中发起异步操作并传入此时被suspended的`coroutine_handle`，从而当异步操作完成，协程可以恢复执行时直接调用`coroutine_handle.resume()`，整个过程中不需要任何同步机制，流程示意如下图：

```text
Time     Thread 1                           Thread 2
  |      --------                           --------
  |      ....                               Call OS - Wait for I/O event
  |      Call await_ready()                    |
  |      <supend-point>                        |
  |      Call await_suspend(handle)            |
  |        Store handle in operation           |
  V        Start AsyncFileRead ---+            V
                                  +----->   <AsyncFileRead Completion Event>
                                            Load coroutine_handle from operation
                                            Call handle.resume()
                                              <resume-point>
                                              Call to await_resume()
                                              execution continues....
           Call to AsyncFileRead returns
         Call to await_suspend() returns
         <return-to-caller/resumer>
```

注意：

- 如果发起异步操作后将`coroutine_handle`传给其他线程，则**其他线程有可能在当前协程依然在`await_suspend()`时调用`coroutine_handle.resume()`并恢复协程执行，从而出现并发**，需要特别注意
- 当调用`coroutine_handle.resume()`时，会首先调用`await_resume()`获取结果并析构Awaiter对象（即`await_suspend()的this`），因此**一旦将`coroutine_handle`传递出去后，不应再使用任何涉及`this`Awaiter或是`.promise()`的数据**
- `await_suspend()`内部的**局部变量**依然是可以安全访问的

### Comparison to Stackful Coroutines

`TODO`

## Avoiding memory allocations

对于常规的异步操作来说，每一步操作往往需要保持一定的状态信息来追踪操作的进度，从而可能会需要较多的状态内存分配与销毁，而在coroutine中，由于coroutine frame会保存coroutine的状态，因此**通过在Awaiter对象上保存状态（利用coroutine frame），可以实现生命期管理以及减低内存分配回收的频率**

**可以将coroutine frame理解为一个高性能arena memory allocator**，由编译器负责分析coroutine frame上需要保存的对象并相应的分配内存与回收

## An example: Implementing a simple thread-synchronisation primitive

实现一个异步set的对象，基本用法如下：

```cpp
T value;
async_manual_reset_event event;

// A single call to produce a value
void producer()
{
  value = some_long_running_computation();

  // Publish the value by setting the event.
  event.set();
}

// Supports multiple concurrent consumers
task<> consumer()
{
  // Wait until the event is signalled by call to event.set()
  // in the producer() function.
  co_await event;

  // Now it's safe to consume 'value'
  // This is guaranteed to 'happen after' assignment to 'value'
  std::cout << value << std::endl;
}
```

对象的接口设计如下：

```cpp
class async_manual_reset_event
{
public:

  async_manual_reset_event(bool initiallySet = false) noexcept;

  // No copying/moving
  async_manual_reset_event(const async_manual_reset_event&) = delete;
  async_manual_reset_event(async_manual_reset_event&&) = delete;
  async_manual_reset_event& operator=(const async_manual_reset_event&) = delete;
  async_manual_reset_event& operator=(async_manual_reset_event&&) = delete;

  bool is_set() const noexcept;

  struct awaiter;
  awaiter operator co_await() const noexcept;

  void set() noexcept;
  void reset() noexcept;

private:

  friend struct awaiter;

  // - 'this' => set state
  // - otherwise => not set, head of linked list of awaiter*.
  mutable std::atomic<void*> m_state;
};
```

### 1. Defining the Awaiter

```cpp
struct async_manual_reset_event::awaiter
{
  awaiter(const async_manual_reset_event& event) noexcept
  : m_event(event)
  {}

  // 如果已经set就不需要再等待，可以直接继续执行
  bool await_ready() const noexcept
  {
    return m_event.is_set();
  }

  bool await_suspend(std::coroutine_handle<> awaitingCoroutine) noexcept
  {
    // Special m_state value that indicates the event is in the 'set' state.
    const void* const setState = &m_event;

    // Remember the handle of the awaiting coroutine.
    m_awaitingCoroutine = awaitingCoroutine;

    // Try to atomically push this awaiter onto the front of the list.
    // 使用acquire是为了确保set()前的写入在这里可见
    void* oldValue = m_event.m_state.load(std::memory_order_acquire);
    do {
      // Resume immediately if already in 'set' state.
      if (oldValue == setState) return false; 

      // Update linked list to point at current head.
      m_next = static_cast<awaiter*>(oldValue);

      // Finally, try to swap the old list head, inserting this awaiter
      // as the new list head.
      // 使用release是为了确保set()能看到这里的写入，即加入链表
    } while (!m_event.m_state.compare_exchange_weak(
              oldValue,
              this,
              std::memory_order_release,
              std::memory_order_acquire));

    // Successfully enqueued. Remain suspended.
    return true;
  }

  // co_await仅需等待，没有返回值，因此await_resume()返回void即可
  void await_resume() noexcept {}

private:

  // awaiting的具体对象
  const async_manual_reset_event& m_event;
  std::coroutine_handle<> m_awaitingCoroutine;
  // 链表的形式保存一系列awaiter
  awaiter* m_next;
};
```

### 2. Filling out the rest of the event class

```cpp
class async_manual_reset_event
{
public:

  // 初始状态可以是set或者是not set
  async_manual_reset_event(bool initiallySet = false) noexcept
   : m_state(initiallySet ? this : nullptr)
  {
  }

  // No copying/moving
  async_manual_reset_event(const async_manual_reset_event&) = delete;
  async_manual_reset_event(async_manual_reset_event&&) = delete;
  async_manual_reset_event& operator=(const async_manual_reset_event&) = delete;
  async_manual_reset_event& operator=(async_manual_reset_event&&) = delete;

  bool is_set() const noexcept
  {
    return m_state.load(std::memory_order_acquire) == this;
  }

  struct awaiter;
  awaiter operator co_await() const noexcept
  {
    return awaiter{*this};
  }

  void set() noexcept
  {
    // Needs to be 'release' so that subsequent 'co_await' has
    // visibility of our prior writes.
    // Needs to be 'acquire' so that we have visibility of prior
    // writes by awaiting coroutines.
    void* oldValue = m_state.exchange(this, std::memory_order_acq_rel);
    if (oldValue != this) {
      // Wasn't already in 'set' state.
      // Treat old value as head of a linked-list of waiters
      // which we have now acquired and need to resume.
      auto* waiters = static_cast<awaiter*>(oldValue);
      while (waiters != nullptr) {
        // Read m_next before resuming the coroutine as resuming
        // the coroutine will likely destroy the awaiter object.
        auto* next = waiters->m_next;
        waiters->m_awaitingCoroutine.resume();
        // 一旦awaiter被析构，如果这里再使用waiters->m_next就会segfault
        waiters = next;
      }
    }
  }
  void reset() noexcept
  {
    void *oldValue = this;
    m_state.compare_exchange_strong(oldValue, nullptr, std::memory_order_acquire);
  }

private:

  friend struct awaiter;

  // - 'this' => set state
  // - otherwise => not set, head of linked list of awaiter*.
  mutable std::atomic<void*> m_state;
};
```

完整的示例见此[co_await_example.cpp](examples/co_await_example.cpp)
