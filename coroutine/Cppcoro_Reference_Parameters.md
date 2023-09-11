# C++ Coroutines: Coroutines and Reference Parameters

[original post](https://toby-allsopp.github.io/2017/04/22/coroutines-reference-params.html)

## 背景 Background

- **协程 Coroutines**
  C++20引入的协程是无栈式的stackless协程，因此在协程切换时整个调用栈都会被销毁（不用保存调用栈从而非常轻量高效），只有局部变量会被保存随后恢复，而对于引用的局部变量，**只有引用本身被拷贝进协程栈**，被引用的实际对象并不会被拷贝，因此很有可能导致无效引用
- **转发引用 Forwarding references**

    ```cpp
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

```cpp
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

```cpp
std::vector<int> v{1, 2, 3, 4};
auto f = [](int i) { return i * 2; };
for (auto&& element : map(v, f)) {
  process(element);
}
```

此时发现变量`v`是多余的，而如果直接消去，**在`map`调用时传入一个临时构造的`std::vector<int>{1, 2, 3, 4}`就会导致未定义行为UB**，展开这种做法下的range-based for实际上是这样的：

```cpp
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

```cpp
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

```cpp
std::vector v{1, 2, 3, 4};
return map(v, f);           // copies v
return map(std::ref(v), f); // stores a reference to v
```

此时需要对原先的`map`修改为处理`std::reference_wrapper`的方式：

```cpp
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

```cpp
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

## 另一个实例：seastar

在尝试将[seastar](https://github.com/JasonYuchen/notes/blob/master/seastar/Introduction.md)官方自带的[memcached](https://github.com/JasonYuchen/notes/blob/master/seastar/Memcached.md)从**continuation-based**改写至**coroutine-based**的过程中，对**协程函数的参数生命周期**有了更深入的理解，[协程的引用参数问题可以参见此](Cppcoro_Reference_Parameters.md)

### 测试环境

- OS: Ubuntu 20.04
- Seastar debug build with Clion 12

### 测试代码

构造一个只支持移动的类`test`，并在构造函数中添加输出信息帮助确认生命周期

```cpp
class test {
  public:
  int _id = -1;
  explicit test(int id) : _id(id) {
    cout << "ctor " << _id << endl;
  }
  test(test&& r) noexcept {
    _id = r._id; r._id = -1;
    cout << "move ctor from " << _id << endl;
  }
  test& operator=(test&& r) noexcept {
    _id = r._id; r._id = -1;
    cout << "move assign from " << _id << endl;
    return *this;
  }
  ~test() {
    cout << "dtor " << _id << endl;
  }

  seastar::future<> handle() {
    cout << "start" << endl;
    co_await seastar::sleep(1s);
    cout << "done" << endl;
  }
};
```

在循环中调用被测试的函数并且**不等待返回结果立即开始下一轮循环**（这也是本次测试的由来，在seastar应用中通常会循环`listener->accept()`并开始处理连接，且不会等待该连接处理完而是直接准备下一次`accept()`创建新的连接，[见此](https://github.com/JasonYuchen/notes/blob/master/seastar/Comprehensive_Tutorial.md#%E7%BD%91%E7%BB%9C%E6%A0%88-introducing-seastars-network-stack)），从而局部变量应立即析构：

```cpp
int main(int argc, char** argv) {
  seastar::app_template app;
  app.run(argc, argv, [] () -> seastar::future<> {
    for (int i = 1; i <= 2; ++i) {
      // 1
      test o(i);
      (void)o.handle();
      // 2
      // (void)handle(std::move(o)); // (void)handle(test{i});
      // 3
      // (void)handle_lref(o);
      // 4
      // (void)handle_rref(std::move(o));
      // 5
      // auto p = seastar::make_lw_shared<test>();
      // (void)handle_sp(p);
    }
    co_await seastar::sleep(3s);
  });
}
```

### 测试结果

1. 调用成员函数（**不安全**）

    ```cpp
    seastar::future<> handle() {
      cout << "start" << endl;
      co_await seastar::sleep(1s);
      cout << "done" << endl;
    }
    ```

    可以发现，输出`start`后抵达第一个暂停点`co_await seastar::sleep(1s)`，此时协程挂起返回，并且开启下一轮循环，因此析构函数被调用并输出`dtor 1`，同理输出`dtor 2`，挂起结束后才输出函数结束前的`done`，显然在成员函数内输出`done`时**对象已经被析构**

    ```text
    ctor 1
    start
    dtor 1
    ctor 2
    start
    dtor 2
    done
    done
    ```

2. 移动一份对象交给协程（**安全**）

    ```cpp
    seastar::future<> handle(test o) {
      co_return co_await o.handle();
    }
    ```

    可以发现，移动构造函数被调用，协程本地有了一份有效的拷贝（局部变量被移动进协程栈），因此析构的是已经无效的对象所以输出`dtor -1`，很明显**直到协程真正结束（输出`done`）后，有效的对象才被析构**输出`dtor 1`和`dtor 2`

    注意输出两次`move ctor from 1`分别代表一次手动`move`以及一次从[局部变量移动进协程栈](https://github.com/JasonYuchen/notes/blob/master/coroutine/Cppcoro_Understanding_Promise.md#2-copying-parameters-to-the-coroutine-frame)，如果调用方式改为`handle(test{i})`则只会有一次

    ```text
    ctor 1
    move ctor from 1
    move ctor from 1
    start
    dtor -1
    dtor -1
    ctor 2
    move ctor from 2
    move ctor from 2
    start
    dtor -1
    dtor -1
    done
    dtor 1
    done
    dtor 2
    ```

3. 协程左值引用外部变量（与第一种调用成员函数本质上相同,**不安全**）

    ```cpp
    seastar::future<> handle_lref(test& o) {
      co_return co_await o.handle();
    }
    ```

    与第一种情况相等，**在协程恢复后引用已经失效**

    ```text
    ctor 1
    start
    dtor 1
    ctor 2
    start
    dtor 2
    done
    done
    ```

4. 协程右值引用外部变量（**不安全**）

    ```cpp
    seastar::future<> handle_rref(test&& o) {
      co_return co_await o.handle();
    }
    ```

    需要特别注意的是右值引用也是引用，所绑定的临时变量并不会被移动进协程栈，从而**也在协程恢复后失效**，类似[前文描述的情况](#问题-the-problem)

    ```text
    ctor 1
    start
    dtor 1
    ctor 2
    start
    dtor 2
    done
    done
    ```

5. 指针（**安全**）

    ```cpp
    seastar::future<> handle_sp(seastar::lw_shared_ptr<test> o) {
      co_return co_await o->handle();
    }
    ```

    显然采用指针的方式更为简洁高效，智能指针被移动进了协程，由于引用计数的关系对象始终有效，直到协程运行结束返回，**协程栈被析构，此时引用归零对象才真正被析构**

    ```text
    ctor 1
    start
    ctor 2
    start
    done
    dtor 1
    done
    dtor 2
    ```
