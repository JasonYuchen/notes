# C++ Coroutines: Coroutines and Reference Parameters

[original post](https://toby-allsopp.github.io/2017/04/22/coroutines-reference-params.html)

## 背景 Background

- **协程 Coroutines**
  C++20引入的协程是无栈式的stackless协程，因此在协程切换时整个调用栈都会被销毁（不用保存调用栈从而非常轻量高效），只有局部变量会被保存随后恢复，而对于引用的局部变量，**只有引用本身被拷贝进协程栈**，被引用的实际对象并不会被拷贝，因此很有可能导致无效引用
- **转发引用 Forwarding references**

    ```C++
    template<typename T>
    void f(T&& x);

    f(3); // T is int, x is int&&
    int i = 3;
    f(i); // T is int&, x is int&
    const int j = 3;
    f(j); // T is int const&, x is int const&
    ```

## 问题 The problem

假定现在有一个非常简单的协程利用`generator`实现一个懒惰的map操作：

```c++
template<typename T>
using range_value_t = decltype(*std::declval<T>().begin());

template<typename T, typename F>
generator<std::result_of_t<F(range_value_t<T>)>> map(const T& v, F f) {
  for (auto&& element : v) {
    co_yield f(element);
  }
}
```

而这个协程返回的懒惰求值的`generator`提供了`begin()/end()`用于获取一系列值，可以如下使用：

```c++
std::vector<int> v{1, 2, 3, 4};
auto f = [](int i) { return i * 2; };
for (auto&& element : map(v, f)) {
  process(element);
}
```

此时发现变量`v`是多余的，而如果直接消去，**在`map`调用时传入一个临时构造的`std::vector<int>{1, 2, 3, 4}`就会导致未定义行为UB**，展开这种做法下的range-based for实际上是这样的：

```c++
{
  auto&& __range = map(std::vector<int>{1, 2, 3, 4}, f);
  for (auto __begin = __range.begin(), __end = __range.end();
      __begin != __end;
      ++__begin) {
    auto&& element = *__begin;
    process(elements);
  }
}
```

很明显，临时构造的对象`vector`在range-based for还未开始时就已经析构了（`map`函数返回），从而当后续调用`generator::begin`以及`generator::end`懒惰求值时，协程内部所绑定的`const T& v`实际上已经被析构，是一个悬置对象了

> In a coroutine, you can’t rely on references passed as arguments remaining valid for the life of the coroutine. You need to think of them like captures in a lambda.

## 解决方式0：直接拷贝 Copy Already

很显然对于引用失效，只需要每次都`copy/move`了对象，确保协程栈中有对象的副本就可以避免协程恢复后原对象的失效问题，缺点也很明显即对于非临时的变量这种做法也拷贝了一份造成了不必要的性能和空间开销

## 解决方式1：谨慎转发 Clever Forwarding

由于临时变量被引用才会导致上述问题，因此谨慎的使用转发确保协程**引用非临时变量而拷贝/移动了临时变量**，来避免这种问题，例如：

```c++
// 将原先的map接受引用const T& v改为接受值对象T v
template<typename T, typename F>
generator<std::result_of_t<F(range_value_t<T>)>>
map_impl(T v, F f) {
  for (auto&& element : v) {
    co_yield f(element);
  }
}

// 现在的接口map接受万能引用T&&
// 当传入临时对象，例如int&& v时就会调用map_impl<int>，从而完成copy/move
// 当传入非临时对象，例如int& v时就会调用map_impl<int&>，从而避免额外开销
template<typename T, typename F>
auto map(T&& v, F f) {
  // 根据模板参数T显示调用map_impl<T>
  return map_impl<T>(std::forward<T>(v), std::forward<F>(f));
}
```

## 解决方式2：封装引用 Reference Wrapper

使用一个显式的方式来调用协程函数，告知是否可以存储引用例如这种方式：

```c++
std::vector v{1, 2, 3, 4};
return map(v, f);           // copies v
return map(std::ref(v), f); // stores a reference to v
```

此时需要对原先的`map`修改为处理`std::reference_wrapper`的方式：

```c++
template<typename T>
using unwrap_reference_t = decltype(std::ref(std::declval<T>()).get());

template<typename T, typename F>
generator<std::result_of_t<F(range_value_t<unwrap_reference_t<T>>)>> map(T v, F f) {
  for (auto&& element : std::ref(v).get()) {
    co_yield f(element);
  }
}
```

## 解决方式3：不同函数 Different Functions

使用不同函数来处理引用与非引用的方式：

```c++
// 对于引用对象，利用引用折叠，继续调用map_impl<T&&>，从而保持引用
template<typename T, typename F>
auto map_ref(T&& v, F f) {
  return map_impl<T&&>(std::forward<T>(v), std::forward<F>(f));
}

// 常规的转发方式，从而使用copy/move
template<typename T, typename F>
auto map_val(T&& v, F f) {
  return map_impl(std::forward<T>(v), std::forward<F>(f));
}
```

与方式1类似，但是通过区分不同函数，由调用者来显式的保证是传值还是传引用，确保安全
