# C++ Coroutines: Understanding the promise type

[original post](https://lewissbaker.github.io/2018/09/05/understanding-the-promise-type)

## Coroutine Concepts

当代码中使用到了`co_await`, `co_yield`, `co_return`时，编译器就会将当前函数作为协程（可恢复函数）进行编译

## Promise objects

Promise对象通过定义一系列methods，这些methods**在coroutine的特殊点被调用，从而控制coroutine的行为**，每次调用一个coroutine时，都会在coroutine frame中创建一个promise实例，当在函数体`<body-statements>`中使用了任一协程关键词时，编译器就会将函数体转换成形如下述的代码并在相应位置调用promise的方法：

```cpp
{
  co_await promise.initial_suspend();
  try
  {
    <body-statements>
  }
  catch (...)
  {
    promise.unhandled_exception();
  }
FinalSuspend:
  co_await promise.final_suspend();
}
```

- 当协程调用时，**在执行`<body-statements>`前**会执行下述步骤：

  1. (optional) 使用`operator new`分配协程帧
  2. 拷贝函数参数到协程帧
  3. 调用`promise`的构造函数
  4. 调用`promise.get_return_object()`获取需要在第一次暂停时返回给调用者的返回值，保存为局部变量
  5. 调用`co_await promise.initial_suspend()`
  6. 当第5步的调用恢复运行时（立刻恢复或异步恢复），协程开始执行`<body-statements>`

- 当协程**遇到`co_return`时**会额外执行：

  1. 调用`promise.return_void()`或`promise.return_value(<expr>)`，取决于是否有返回值
  2. 逆构造顺序析构所有自动变量
  3. 调用`co_await promise.final_suspend()`

- 当**执行`<body-statement>`抛出异常时**会执行：

  1. 捕获异常并且调用`promise.unhandled_exception()`
  2. 调用`co_await promise.final_suspend()`

- 当**执行到协程体外时**，协程帧就被摧毁，顺序如下：

  1. 调用`promise`的析构函数
  2. 调用函数参数拷贝对象的析构函数
  3. (optional) 使用`operator delete`回收协程帧
  4. 执行流回到caller/resumer

当协程第一次执行到`co_await`内的一个[`<return-to-caller-or-resumer>`](https://github.com/JasonYuchen/notes/blob/master/coroutine/Cppcoro_Understanding_co_await.md#2-awaiting-the-awaiter)点时，或是直接运行到结束时，协程可以被suspended或destroyed，并且将之前`promise.get_return_object()`的结果返回给协程的调用者

### 1. Allocating a coroutine frame

分配协程帧时，编译器会生成调用`operator new`（或是promise特别定义的重载`operator new`），同时分配的空间大小由编译器根据传入参数、promise自身、局部变量、其他编译器特定的存储大小一起决定

在同时满足以下情况中编译器可以实现额外的优化从而避免`operator new`：

- 协程帧的生命周期被严格控制strictly nested在调用者的生命周期内
- 在调用侧call-site编译器就已经可以获得协程帧的大小

此时编译器就可以将**协程帧分配在调用者的激活帧上**（栈上或协程帧上，因为调用者也可以是协程）

注意：Coroutine TS并没有指定哪些场合一定要优化掉内存分配，因此一个coroutine不能被声明为`noexcept`（因为有可能抛出`std::bad_alloc`，除非允许`std::terminate()`）；另外如果promis类定义了`static P::get_return_object_on_allocation_failure()`，则编译器会实际上调用`operator new(size_t, nothrow_t)`并在返回`nullptr`时继续调用`static P::get_return_object_on_allocation_failure()`而不抛出`std::bad_alloc`

#### 自定义栈帧分配 Customising coroutine frame memory allocation

```cpp
struct my_promise_type
{
  void* operator new(std::size_t size)
  {
    void* ptr = my_custom_allocate(size);
    if (!ptr) throw std::bad_alloc{};
    return ptr;
  }

  void operator delete(void* ptr, std::size_t size)
  {
    my_custom_free(ptr, size);
  }

  ...
};
```

对于allocator `TODO`

注意：**即使自定义了栈帧的分配方式，编译器依然可以根据情况直接优化掉`operator new`**

### 2. Copying parameters to the coroutine frame

协程需要将传入的参数复制到协程帧上，从而在协程suspended时，所有参数依然保持有效

- **传值 passed by value**：使用`move`将参数移动到协程帧上
- **传引用 passed by reference**：无论左值还是右值，只有引用本身被拷贝进协程帧，所引用的值并不会
- 对于有普通析构函数的 trivial destructors：如果对象在`<return-to-caller-or-resumer>`后都未被使用过，则编译器可以直接优化掉
- **对于传引用来说，可能有相当多的情况会导致引用失效**，因此通常的perfect-forwarding和万能引用universal-references无法在协程中使用，会导致未定义行为，更多细节[见此](Cppcoro_Reference_Parameters.md)
- 如果参数在`copy/move`时抛出了异常，已经构造好的对象会被逆序析构，协程帧被释放，异常扩散到caller

### 3. Constructing the promise object

当所有参数都被复制到协程帧后，才构造promise对象，从而promise对象的构造函数内就可以安全访问所有协程帧上的参数

编译器会首先查看**是否有promise构造函数的重载可以支持左值引用所有拷贝的参数**，若没有才会fallback到生成并使用默认的构造函数

### 4. Obtaining the return object

在完成promise构造后，协程首先会调用`promise.get_return_object()`来获取返回对象`return-object`，大致流程如下：

```cpp
// Pretend there's a compiler-generated structure called 'coroutine_frame'
// that holds all of the state needed for the coroutine. It's constructor
// takes a copy of parameters and default-constructs a promise object.
struct coroutine_frame { ... };

T some_coroutine(P param)
{
  auto* f = new coroutine_frame(std::forward<P>(param));

  // 先获得返回对象，再进行resume()是由于协程在resume()返回前可能先被destroy
  // 此时协程帧已被释放，无法再使用promise，也无法获得返回对象
  auto returnObject = f->promise.get_return_object();

  // Start execution of the coroutine body by resuming it.
  // This call will return when the coroutine gets to the first
  // suspend-point or when the coroutine runs to completion.
  coroutine_handle<decltype(f->promise)>::from_promise(f->promise).resume();

  // Then the return object is returned to the caller.
  return returnObject;
}
```

### 5. The initial-suspend point

当已经获得`return-object`后，下一步会执行`co_await promise.initial_suspend()`，此时promise类型就可以控制是suspend还是直接执行协程函数体，由于该行调用的的结果被直接丢弃，因此相应的`await_resume()`应该返回`void`

**需要注意的是这一步是在`try-catch`外执行的，因此如果抛出异常就会直接扩散到调用端**，并且协程帧和返回对象也会被析构，此时如果返回对象设计成RAII并析构时会释放协程帧，那么就可以导致double-free，因此最好将`promise.initial_suspend()`设为`noexcept`

通常大部分协程的`initial_suspend()`总是会返回`std::suspend_always`或`std::suspend_never`，都是`noexcept`的

### 6. Returning to the caller

当协程运行到第一个`<return-to-caller-or-resumer>`或是直接运行到结束时，`return-object`就会被返回给协程的调用者

**注意`return-object`的类型和协程的返回类型不必完全相同**，在需要的时候会进行隐式转换

### 7. Returning from the coroutine using `co_return`

当协程遇到`co_return`时，就会转换为`promise.return_void()`或`promise.return_value(<expr>)`，后跟随一个`goto FinalSuspend;`如下：

```cpp
co_return;
-> promise.return_void();

co_return <expr>;
-> <expr>; promise.return_void(); // if <expr> has type void
-> promise.return_value(<expr>);  // if <expr> doesn't have type void
```

对于直接运行到结束而**没有`co_return`的协程，等同于在末尾隐式加上`co_return`**，而此时如果对应的`promise_type`没有`return_void()`则是未定义行为

注意当`<expr>`或者是`promise.return_void()`或`promise.return_value(<expr>)`抛出异常，则**异常仍然会被扩散到`promise.unhandled_exception()`**

### 8. Handling exceptions that propagate out of the coroutine body

当异常从协程抛出后就会调用`promise.unhandled_exception()`，通常做法是**使用`std::current_exception()`来获得抛出的异常**，并且存储后待合适的上下文处理

目前的Coroutines TS并没有明确指出如果`promise.unhandled_exception()`直接再抛出异常时的行为，但是尽可能`initial_suspend()`，`final_suspend()`和`unhandled_exception()`不要抛出异常到外层

`TODO`

### 9. The final-suspend point

在协程运行完成，已经获得返回对象或是异常后，会执行最后的暂停点，即`co_await promise.final_suspend()`，此时根据`final_suspend()`的定义，可以执行额外的任务，例如发布结果、通知完成、或resume另一个函数

注意**在`final_suspend()`中唯一能对协程做的就是`destroy()`，调用`resume()`是未定义行为**，虽然在`final_suspend()`中也可以不suspend，但是**通常推荐suspend**，并且由于只能`destroy()`（通常是相关对象的RAII语义下释放协程时调用），从而让编译器可以更明确协程的生命周期并做出优化

### 10. How the compiler chooses the promise type

**promise的类型由`std::coroutine_traits`类来决定**，例如有协程`task<float> foo(std::string x, bool flag)`，则编译器就会通过`typename coroutine_traits<task<float>, std::string, bool>::promise_type`来推断promise的类型，对于类的协程成员函数，对象本身的类型也作为模板参数传给`std::coroutine_traits`：

```cpp
task<void> my_class::method1(int x) const;
-> typename coroutine_traits<task<void>, const my_class&, int>::promise_type;

task<foo> my_class::method2() &&;
-> typename coroutine_traits<task<foo>, my_class&&>::promise_type;
```

而在`std::coroutine_traits`中，默认的`promise_type`通过寻找协程函数返回值的嵌套`promise_type`来获得，大致如下：

```cpp
namespace std
{
  template<typename RET, typename... ARGS>
  struct coroutine_traits<RET, ARGS...>
  {
    using promise_type = typename RET::promise_type;
  };
}
```

因此对于协程的返回值，可以**通过自己在返回值类型内部定义嵌套类`promise_type`**来控制编译器使用自己定义的promise类型：

```cpp
template<typename T>
struct task
{
  using promise_type = task_promise<T>;
  ...
};
```

另外对于无法自己控制的返回值类型，例如协程返回`std::optional<T>`，则可以**通过特化`coroutine_traits`来指定对应的promise类型**：

```cpp
namespace std
{
  template<typename T, typename... ARGS>
  struct coroutine_traits<std::optional<T>, ARGS...>
  {
    using promise_type = optional_promise<T>; // 自己定义的用于std::optional<T>的promise类型
  };
}
```

### 11. Identifying a specific coroutine activation frame

resume或destroy一个协程时，往往需要一定机制来**识别协程帧，即`coroutine_handle`**，接口和说明[见此](https://github.com/JasonYuchen/notes/blob/master/coroutine/Cppcoro_Understanding_co_await.md#coroutine-handles)

注意：**`coroutine_handle`没有RAII语义**，必须显式调用`.destroy()`进行资源回收

### 12. Customising the behaviour of `co_await`

**promise类型可以定制每一个`co_await`的行为**（`co_await`的行为[见此](https://github.com/JasonYuchen/notes/blob/master/coroutine/Cppcoro_Understanding_co_await.md)），仅需**在promise类型内部定义`await_transform()`**即可，随后编译器会转换每一个`co_await <expr>`变为`co_await promise.await_transform(<expr>)`，优点如下：

- **通常不是awaitable的类型也可以awaiting**，例如`std::optional<T>`

    ```cpp
    template<typename T>
    class optional_promise
    {
      ...

      template<typename U>
      auto await_transform(std::optional<U>& value)
      {
        class awaiter
        {
          std::optional<U>& value;
        public:
          explicit awaiter(std::optional<U>& x) noexcept : value(x) {}
          // 当optional已经有数据时，不需要suspend
          bool await_ready() noexcept { return value.has_value(); }
          void await_suspend(std::experimental::coroutine_handle<>) noexcept {}
          // 当resume时，已经有数据
          U& await_resume() noexcept { return *value; }
        };
        return awaiter{ value };
      }
    };
    ```

- **显式禁止一些类型被awaiting**，即`await_transform`被禁止

    ```cpp
    template<typename T>
    class generator_promise
    {
      ...

      // Disable any use of co_await within this type of coroutine.
      template<typename U>
      std::experimental::suspend_never await_transform(U&&) = delete;

    };
    ```

- **修改awaitable对象的实际awaiting行为**，例如要求一个`co_await`总是被同一个executor执行，例子来自cppcoro

    ```cpp
    template<typename T, typename Executor>
    class executor_task_promise
    {
      Executor executor;

    public:

      template<typename Awaitable>
      auto await_transform(Awaitable&& awaitable)
      {
        using cppcoro::resume_on;
        return resume_on(this->executor, std::forward<Awaitable>(awaitable));
      }
    };
    ```

注意：一旦定义了`await_transform`，编译器就会转换所有`co_await`，**如果只想自定义一部分`co_await`，则需要显式的提供重载`await_transform`**并调用默认的做法

### 13. Customising the behaviour of `co_yield`

当出现`co_yield`时，会被编译器转换为`co_await promise.yield_value(<expr>)`，从而promise类型也可以自定义`co_yield`的行为

注意与`co_await`不同的是，**`co_await`需要显式定义`await_transform() = delete`来禁止**默认的`co_await`支持，**而`co_yield`没有默认的行为，需要显式定义`yield_value()`来支持**`co_yield`支持

```cpp
template<typename T>
class generator_promise
{
  T* valuePtr;
public:
  ...

  std::suspend_always yield_value(T& value) noexcept
  {
    // Stash the address of the yielded value and then return an awaitable
    // that will cause the coroutine to suspend at the co_yield expression.
    // Execution will then return from the call to coroutine_handle<>::resume()
    // inside either generator<T>::begin() or generator<T>::iterator::operator++().
    valuePtr = std::addressof(value);
    return {};
  }
};
```
