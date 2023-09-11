# C++ Coroutines: Understanding Symmetric Transfer

[original post](https://lewissbaker.github.io/2020/05/11/understanding_symmetric_transfer)

## The stack-overflow problem

`TODO`

## Enter "symmetric transfer"

`await_suspend`接口的返回值可以是：

- `void`
- `bool`
- `std::coroutine_handle<T>` (symmetric trasnfer)
  根据该coroutine handle来确定执行流是非应该被**对称转移symmetrically transferred**给返回的coroutine，同时新的`std::noop_coroutine()`来生成一个不执行任何操作的coroutine handle代表着不进行执行流转移

当编译器遇到返回`std::coroutine_handle<T>`的`await_suspend`时，就会生成如下执行流（返回`void`和`bool`的情况不变）

```cpp
{
  decltype(auto) value = <expr>;
  decltype(auto) awaitable =
      get_awaitable(promise, static_cast<decltype(value)&&>(value));
  decltype(auto) awaiter =
      get_awaiter(static_cast<decltype(awaitable)&&>(awaitable));
  if (!awaiter.await_ready())
  {
    using handle_t = std::coroutine_handle<P>;

    //<suspend-coroutine>

    auto h = awaiter.await_suspend(handle_t::from_promise(p));
    h.resume();
    //<return-to-caller-or-resumer>
    
    //<resume-point>
  }

  return awaiter.await_resume();
}
```

从而编译器可以实施**尾递归tail-calls优化**，或者采用**trampolining loop**如下（当下编译器不保证实施尾递归优化），实现代码[见此](Cppcoro_Understanding_the_Compiler_Transform.md#step-13-implementing-symmetric-transfer-and-the-noop-coroutine)，若要实施尾递归优化，则要保证（trampolining loop无此限制）：

- the **calling convention** supports tail-calls and is the same for the caller and callee;
  由于此部分完全有编译器来实现，因此编译器可以选择支持tail-calls的calling conversion
- the **return-type** is the same;
  `.resume()`总是返回`void`
- there are **no non-trivial destructors** that need to be run after the call before returning to the caller; and
  此条目的是为了在实施tail-call时能够立即释放当前的栈帧，在当前栈上的对象必须在返回前就已经终止生命周期，而协程将相关参数都保存在协程栈coroutine state上，本地变量则在要退出时已经结束生命周期
- the call is **not inside a try/catch block**.
  由于用户提供的协程代码都被一个`try/catch`所包围，因此编译器在实现此部分时通过特别的操作将`.resume()`放置在异常处理的代码块外，[见此](Cppcoro_Understanding_the_Compiler_Transform.md#step-10-implementing-unhandled_exception)

```cpp
void std::coroutine_handle<void>::resume() const {
    __coroutine_state* s = state_;
    do {
        s = s->__resume(s);
    } while (/* some condition */);
}
```

## Symmetric Transfer as the Universal Form of `await_suspend`

```cpp
void my_awaiter::await_suspend(std::coroutine_handle<> h) {
  this->coro = h; // the next/parent coroutine we want to resume, call h.resume() when this is done
  enqueue(this);  // executing this task, and call resume later
}

// same as

bool my_awaiter::await_suspend(std::coroutine_handle<> h) {
  this->coro = h;
  enqueue(this);
  return true;
}

// same as

std::noop_coroutine_handle my_awaiter::await_suspend(
    std::coroutine_handle<> h) {
  this->coro = h;
  enqueue(this);
  return std::noop_coroutine();
}
```

```cpp
bool my_awaiter::await_suspend(std::coroutine_handle<> h) {
  this->coro = h;
  if (try_start(this)) {
    // Operation will complete asynchronously.
    // Return true to transfer execution to caller of
    // coroutine_handle::resume().
    return true;
  }

  // Operation completed synchronously.
  // Return false to immediately resume the current coroutine.
  return false;
}

// same as

std::coroutine_handle<> my_awaiter::await_suspend(std::coroutine_handle<> h) {
  this->coro = h;
  if (try_start(this)) {
    // Operation will complete asynchronously.
    // Return std::noop_coroutine() to transfer execution to caller of
    // coroutine_handle::resume().
    return std::noop_coroutine();
  }

  // Operation completed synchronously.
  // Return current coroutine's handle to immediately resume
  // the current coroutine.
  return h;
}
```

> - unconditionally return to `.resume()` caller, use the `void`-returning flavour
> - conditionally return to `.resume()` caller or resume current coroutine use the `bool`-returning flavour
> - resume another coroutine use the symmetric-transfer flavour.
