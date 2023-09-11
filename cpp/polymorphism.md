# 多态 Polymorphism

*When OO is not "OO"* by Michael Spertus in CPP-Summit 2020.

## 动态多态 Dynamic Polymorphism

### 虚函数的方式实现动态多态

```cpp
struct Animal {
  virtual std::string name() = 0;
  virtual std::string eats() = 0;
};

struct Cat : public Animal {
  std::string name() override { return "cat"; }
  std::string eats() override { return "delicious mice"}
};

int main() {
  std::unique_ptr<Animal> a = std::make_unique<Cat>();
  std::cout << a->name() << " eats " << a->eats();
}
```

### 虚函数的性能

下列测试情况中，仅修改了一行代码`a->f(i, 10) => a->f(10, i)`就可以使得非虚拟函数调用的情况性能显著提升，**虚拟函数本身多一层间接调用往往不是性能瓶颈**，仅仅有可能会影响数据局部性等导致性能略有下降（一层间接调用相比于函数自身的开销往往微不足道）

虚拟函数的存在导致编译器等到运行时才知道真正调用的函数，从而**阻止了编译期的内联、函数级优化**，例如下面测试的非虚拟函数版本，`log(10)`在编译期就可以被计算完成，因此**大量使用虚拟函数会限制编译器的优化**

```cpp
class A {
 public:
  virtual int f(double d, int i) {
    return static_cast<int>(d * log(d)) * i;
  }
}

int main() {
  boost::progress_timer t;
  A *a = new A();
  int ai = 0;
  for (int i = 0; i < 100000000; ++i) {
    // case1: 15.83s if virtual
    //        15.82s if non-virtual
    ai += a->f(i, 10);

    // case2: 15.36s if virtual
    //         0.22s if non-virtual
    ai += a->f(10, i);
  }
  std::cout << ai << std::endl;
}
```

## 静态多态 Static Polymorphism

### 模板与Concept

- 性能更好：对象创建在栈上，没有虚函数调用
- 更加灵活：没有继承，但是类型安全变弱

```cpp
template<typename T>
concept Animal = requires(T a) {
  { a.eats() } -> std::convertible_to<std::string>;
  { a.name() } -> std::convertible_to<std::string>;
};

struct Cat {
  std::string eats() { return "delicious mice"; }
  std::string name() { return "cat"; }
};

struct Dog {
  std::string eats() { return "sleeping cats"; }
  std::string name() { return "dog"; }
};

int main() {
  Animal auto a = Cat();
  std::cout << a.name() << " eats " << a.eats();
}
```

### 鸭子类型与`std::variant`

`std::variant`可以装入模板参数指定的一系列类型中的一种，并且使用`std::visit`来访问对象

- 性能几乎与模板一样快
- 几乎和传统OO一样动态，例如可以使用`std::set<Animal>`等

```cpp
using Animal = std::variant<Cat, Dog>;

int main() {
  Animal a = Cat();
  std::cout << std::visit(
      [](auto& object) {
        return object.name() + " eats " + object.eats();
      },
      a);
}
```

当需要额外加方法重载时需要额外借助可变参模板用于继承调用运算符`operator()`，仅需定义一次就可以在需要的情况下使用，又被称为**重载模式overload pattern**：

```cpp
// only do this once, inherit all call operator
template<typename... Ts>
struct overload : Ts... { using Ts::operator()...; };
// only do this once before C++20
// convert construction to template instantiation
template<typename... Ts>
overload(Ts...) -> overload<Ts...>;

int main() {
  overload life([](Cat&) { return 12; },
                [](Dog&) { return 10; });
  std::variant<Cat, Dog> a = Cat();
  std::cout << std::visit(life, a); // output 12
}
```

### 奇异递归模板模式 CRTP

CRTP通过派生类继承基类且自身就是基类的特化，从而提供了静态多态的方式，**基类的所有方法就会被通过静态转换成派生类的方法**，免除了虚函数调用

```cpp
// https://stackoverflow.com/questions/4173254/what-is-the-curiously-recurring-template-pattern-crtp
template<typename T>
struct Base {
  void foo() {
    (static_cast<T*>(this))->foo();
  }
}

struct Derived1 : public Base<Derived1> {
  void foo() {
    std::cout << "derived 1 foo" << std::endl;
  }
}

struct Dervied2 : public Base<Derived2> {
  void foo() {
    std::cout << "derived 2 foo" << std::endl;
  }
}

template<typename T>
void process(Base<T>* b) {
  b->foo();
}

int main()
{
  Derived1 d1;
  Derived2 d2;
  process(&d1); // output: derived 1 foo
  process(&d2); // output: derived 2 foo
  return 0;
}
```

C++11中提供了共享所有权的智能指针`std::shared_ptr`，而当一个类方法需要向外提供自身的智能指针时，不能简单的直接`return std::shared_ptr<T>(this)`（显然会导致自身被不同的智能指针单独管理从而出现内存错误）

标准库提供了`std::enable_shared_from_this<T>`来解决，也是利用了CRTP的原理，其用法如下：

```cpp
class Foo : public std::enable_shared_from_this<Foo> {
 public:
  void do() {
    std::shared_ptr<Foo> sp{std::shared_from_this()};
    // do domething with sp
  }
}
```

`std::enable_shared_from_this`的原理就是在基类中使用`weak_ptr`来记录需要`shared_from_this`的类，并且在构造过程中，判断是否来自`weak_ptr`：

```cpp
// simplified source code from libstdc++
template<typename T>
class enable_shared_from_this {
 public:
  shared_ptr<T> shared_from_this() {
    return shared_ptr<T>(this->weak_this);
  }
 private:
  template<typename>
  friend class shared_ptr;

  template<typename U>
  void _M_weak_assign(U* p, const shared_count<>& n) {
    weak_this._M_assign(p, n);
  }

  mutable weak_ptr<T> weak_this;
};

template<typename _Yp, typename _Yp2 = typename remove_cv<_Yp>::type>
typename enable_if<__has_esft_base<_Yp2>::value>::type
_M_enable_shared_from_this_with(_Yp* __p) noexcept
{
    // 假如是继承了enable_shared_from_this的基类，就初始化weak_ptr
    if(auto __base = __enable_shared_from_this_base(_M_refcount, __p))
        __base->_M_weak_assign(const_cast<_Yp2*>(__p), _M_refcount);
}

template<typename _Yp, typename _Yp2 = typename remove_cv<_Yp>::type>
typename enable_if<!__has_esft_base<_Yp2>::value>::type
_M_enable_shared_from_this_with(_Yp*) noexcept { }

// from shared_ptr_base.h class __weak_ptr, derived by weak_ptr

void _M_assign(_Tp* __ptr, const __shared_count<_Lp>& __refcount) noexcept {
  // 第一个使用该对象的负责初始化其weak_ptr
  if (use_count() == 0) {
    _M_ptr = __ptr;
    _M_refcount = __refcount;
  }
}
```
