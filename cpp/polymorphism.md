# 多态 Polymorphism

*When OO is not "OO"* by Michael Spertus in CPP-Summit 2020.

## 动态多态 Dynamic Polymorphism

### 虚函数的方式实现动态多态

```C++
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

```c++
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

```C++
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

```C++
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

```C++
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

`TODO`
