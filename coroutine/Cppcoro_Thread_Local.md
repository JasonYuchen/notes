# Thread Local Storage in Cpp Coroutines

一组协程本身可以由固定的单个线程来执行或是跨线程执行，取决于`awaiter::await_suspend`时的行为，即由程序员自定义

当协程跨线程使用时，TLS也会随之改变，因此在协程中应谨慎或避免使用TLS

```cpp
thread_local int tls = 1;

cout << tls << std::endl;  // 1
co_await SomeAsyncApi();   // maybe resumed in another thread
cout << tls << std::endl;  // maybe not 1, it denpends
```

参考[这里的讨论](https://github.com/GorNishanov/coroutines-ts/issues/2)

> When a coroutine is executing, it gets the same view of the thread-local storage as whomever called or resumed the coroutine.
