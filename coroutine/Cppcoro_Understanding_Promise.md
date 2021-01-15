# C++ Coroutines: Understanding the promise type

[original post](https://lewissbaker.github.io/2018/09/05/understanding-the-promise-type)

## Coroutine Concepts

当代码中使用到了`co_await`, `co_yield`, `co_return`时，编译器就会将当前函数作为协程（可恢复函数）进行编译

## Promise objects

Promise对象通过定义一系列methods，这些methods在coroutine的特殊点被调用，从而控制coroutine的行为，每次调用一个coroutine时，都会在coroutine frame中创建一个promise实例，当在函数体`<body-statements>`中使用了任一协程关键词时，编译器就会将函数体转换成形如下述的代码并在相应位置调用promise的方法：

```C++
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

- 当协程调用时，在执行`<body-statements>`前会执行下述步骤：

  1. (optional) 使用`operator new`分配协程帧
  2. 拷贝函数参数到协程帧
  3. 调用`promise`的构造函数
  4. 调用`promise.get_return_object()`获取需要在第一次暂停时返回给调用者的返回值，保存为局部变量
  5. 调用`co_await promise.initial_suspend()`
  6. 当第5步的调用恢复运行时（立刻恢复或异步恢复），协程开始执行`<body-statements>`

- 当协程遇到`co_return`时会额外执行：

  1. 调用`promise.return_void()`或`promise.return_value(<expr>)`取决于是否有返回值
  2. 逆构造顺序析构所有自动变量
  3. 调用`co_await promise.final_suspend()`

- 当执行`<body-statement>`抛出异常时会执行：

  1. 捕获异常并且调用`promise.unhandled_exception()`
  2. 调用`co_await promise.final_suspend()`

- 当执行到协程体外时，协程帧就被摧毁，顺序如下：

  1. 调用`promise`的析构函数
  2. 调用函数参数拷贝对象的析构函数
  3. (optional) 使用`operator delete`回收协程帧
  4. 执行流回到caller/resumer

### 1. Allocating a coroutine frame

### 2. Copying parameters to the coroutine frame

### 3. Constructing the promise object

### 4. Obtaining the return object

### 5. The initial-suspend point

### 6. Returning to the caller

### 7. Returning from the coroutine using `co_return`

### 8. Handling exceptions that propagate out of the coroutine body

### 9. The final-suspend point

### 10. How the compiler chooses the promise type

### 11. Identifying a specific coroutine activation frame

### 12. Customising the behaviour of `co_await`

### 13. Customising the behaviour of `co_yield`

### 14. Summary
