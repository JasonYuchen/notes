# C++17 In Detail

Only part of the changes are noted here.

## 1. Fixes and Depreaction

- **针对直接列表初始化的新`auto`推导规则**

  - 对于大括号直接初始化仅包含单个元素的，从该元素推导（而不会像C++11中推导为`std::initializer_list<T>`）
  - 对于大括号直接初始化包含多个元素的，无法推导类型

  ```C++
  auto x1 = { 1, 2 };     // decltype(x1) is std::initializer_list<int>
  auto x2 = { 1, 2.0 };   // error: cannot deduce element type
  auto x3{ 1, 2 };        // error: not a single element
  auto x4 = { 3 };        // decltype(x4) is std::initializer_list<int>
  auto x5{ 3 };           // decltype(x5) is int
  ```

## 2. Language Clarification

- **严格的表达式求值顺序**
  - 在C++17之前，`f(a(x), b, c(y))`中`x, y, a(x), b, c(y)`的求值顺序是任意的，因此对于`f(unique_ptr<T>(new T), g())`中有可能出现`new T -> g() -> unique_ptr<T>`这样的顺序，而`g()`抛出异常导致出现内存泄漏
  - 在C++17开始，**多个参数之间的顺序依然不保证，但是每个参数自身的依赖链会连续求值，类似DFS顺序**，即当选择首先求值`c(y)`时，一定会立即求值`y`直到获得`c(y)`才会求值其他参数

    - 链式调用中严格从左到右求值`a(expA).b(expB).c(expC)`
    - 下述代码中均是先求值`a`再求值`b`：

      ```C++
      a.b
      a->b
      a->*b
      a(b1, b2, b3) // b1, b2, b3 - in any order
      b @= a        // '@' means any operator
      a[b]
      a << b
      a >> b
      ```

- **值类型 Value Categories**
  - **lvalue**：一个拥有实体的表达式，可以取地址
  - **glvalue = lvalue + xvalue**：generalised lvalue，一个可以产生对象位置的表达式或是函数
  - **xvalue**：eXpiring lvalue，生命周期即将结束的对象，可以`move`走，本身可以重用
  - **rvalue = xvalue + prvalue**：
  - **prvalue**：pure rvalue，没有名字的对象，不可以取地址，可以`move`走
  
  ```C++
  class X { int a; };
  X{10} // this expression is prvalue
  X x;  // x is lvalue
  x.a   // it's lvalue (location)
  ```

  > **prvalues** perform initialisation, **glvalues** describe locations

- **分配对象时内存对齐**

  ```C++
  class alignas(16) vec4 {
    float x, y, z, w;
  };

  auto pVec = new vec4[1000];

  operator new[](sizeof(vec4), align_val_t(alignof(vec4)))
  ```

## 3. General Language Features

- **结构化绑定 Structured Binding**
  结构化绑定可以使用在以下场合：
  1. 数组类型，则元素可以一一对应
  2. 提供了`std::tuple_size<>`和`get<N>()`方法的类（例如`std::pair`和`std::tuple`）
  3. 只有非静态公有成员变量的类

  ```C++
  // 1.
  double myArray[3] = { 1.0, 2.0, 3.0 };
  auto [a, b, c] = myArray;

  // 2.
  std::pair myPair(0, 1.0f);
  const auto& [a, b] = myPair;

  // 3.
  struct Point {
    double x;
    double y;
  };
  Point myPoint{0.0, 0.0};
  auto&& [x, y] = myPoint;
  ```

- **内联变量 Inline Variables**
  类成员的静态变量可以直接在头文件内声明并定义，而不需要在cpp文件内再定义，注意`constexpr`是隐式inline的，C++17保证所有编译单元都看到的同一份静态变量
  
  ```C++
  struct MyClass {
    inline static int sValue = 999;
  };
  ```

## 4. Templates

- **类模板参数推导 Template Argument Deduction for Class Templates**
  模板函数的模板参数可以根据函数参数自动推导，这种方式拓展到了类模板，模板类的模板参数可以根据构造函数的参数自动推导
  
  ```C++
  using namespace std::string_literals;
  std::pair myPair(42, "hellow world"s); // std::pair<int, std::string>
  std::array arr{1, 2, 3};               // std::array<int, 3>
  ```

  编译器通过**推导规则Deduction Guides**来尝试推导模板类的模板参数，推导规则包含编译器隐式生成规则和用户自定义规则两类：

  ```C++
  // custom deduction guide for std::array
  template<class T, class... U>
  array(T, U...) -> array<T, 1 + sizeof...(U)>;

  // custom deduction guide for overload
  template<class... Ts>
  struct overload : Ts... { using Ts::operator()...; };

  template<class... Ts>
  overload(Ts...) -> overload<Ts...>;
  ```
  
  采用**overload pattern**可以将一系列lambdas转换为一系列classes并可以继承，[应用场合例如静态多态](https://github.com/JasonYuchen/notes/blob/master/cpp/polymorphism.md#%E9%B8%AD%E5%AD%90%E7%B1%BB%E5%9E%8B%E4%B8%8Estdvariant)

- **折叠表达式 Fold Expressions**
  在可变参数模板variadic templates中采用折叠表达式的方式进行参数展开（一定程度上可以替代递归展开）

  ```C++
  template<typename ...Args> auto sum2(Args ...args) {
    return (args + ...);
  }
  auto value = sum2(1, 2, 3, 4); // auto value = 1 + (2 + (3 + 4));

  template<typename ...Args> void foldPrint(Args&& ...args) {
    (std::cout << ... << std::forward<Args>(args)) << '\n';
  }
  foldPrint("hello", 10, 20, 30);
  ```

  |Expression|Name|Expansion|
  |:-|:-|:-|
  |`(... op e)`|unary left fold|`((e1 op e2) op ...) op eN`|
  |`(init op ... op e)`|binary left fold|`(((init op e1) op e2) op ...) op eN`|
  |`(e op ...)`|unary right fold|`e1 op (... op (eN-1 op eN)`|
  |`(e op ... op init)`|binary right fold|`e1 op (... op (eN-1 op (eN op init)))`|

  *`op`是以下任意一个运算符：`+ - * / % ^ & | = < > << >> += -= *= /= %= ^= &= |= <<= >>= == != <= >= && || , .* ->*`*

  *对于`&& || ,`这三个运算有空参数默认值为`true false void()`，因此对于前述的`sum2()`就会报错因为`+`没有空参数默认值*
- **编译期判断 `if constexpr`**
  在编译期就执行判断，并丢弃掉不符合判断的代码（从而运行时不会运行这些代码），需要特别注意的是，**只会丢弃依赖判断内容的表达式，不依赖的表达式不会被移除**：
  - 假如传入的符合`std::is_integral_v<T>`，则`else`分支中的`execute(t)`由于依赖了`T t`因此会被移除，而`strange syntax`并不依赖，会被保留并执行编译，此时就可能因为语法问题编译出错
  - 在符合`std::is_integral_v<T>`的分支内`static_assert(sizeof(int) == 100)`由于不依赖`T t`因此总是会报错，可以改为`static_assert(sizeof(T) == 100)`从而只有此分支被保留时才会报错

  ```C++
  template <typename T>
  void calculate(T t) {
    if constexpr (std::is_integral_v<T>) {
      // ...
      static_assert(sizeof(int) == 100); // static_assert(sizeof(T) == 100);
    } else {
      execute(t);
      strange syntax
    }
  }
  ```

  采用`if constexpr`可以**显著简化原先需要SFINAE、tag dispatching才能实现的模板代码**：

  ```C++
  // SFINAE
  template <typename T>
  std::enable_if_t<std::is_integral_v<T>, T> simpleTypeInfo(T t) {
    std::cout << "foo<integral T> " << t << '\n';
    return t;
  }
  template <typename T>
  std::enable_if_t<!std::is_integral_v<T>, T> simpleTypeInfo(T t) {
    std::cout << "not integral \n";
    return t;
  }

  // if constexpr
  template <typename T>
  T simpleTypeInfo(T t) {
    if constexpr (std::is_integral_v<T>) {
      std::cout << "foo<integral T> " << t << '\n';
    } else {
      std::cout << "not integral \n";
    }
    return t;
  }
  ```

## 5. Standard Attributes

- `[[fallthrough]]`
  
  ```C++
  switch (c) {
    case 'A':
      f();           // warning, fallthrough is perhaps a programmer error
    case 'B':
      g();
    [[fallthrough]]; // suppressed
    case 'C':
      h();
  }
  ```

- `[[maybe_unused]]`

  ```C++
  static void impl1() {...} // compilers may warn about this
  [[maybe_unused]] static void impl2() {...} // suppressed

  void foo() {
    int x = 42; // compilers may warn about this
    [[maybe_unsed]] int y = 53; // suppressed
  }
  ```

- `[[nodiscard]]`

  ```C++
  [[nodiscard]] int compute();
  void test() {
    compute();  // warning, return value is discarded
  }

  enum class [[nodiscard]] ErrorCode {  // warning if ErrorCode is discarded
    OK,
    Fatal,
    Unknown,
  };
  ```

- `[[deprecated("message")]]` or `[[deprecated]]`

  ```C++
  namespace [[deprecated("use BetterUtils")]] GoodUtils {
    void doStuff();  // when used, warning: 'GoodUtils' is deprecated: use BetterUtils
  }
  ```

- `[[noreturn]]`
- `[[carries_dependency]]`

## 6. `std::optional`

- **创建对象**
  
  ```C++
  // empty:
  std::optional<int> oEmpty;
  std::optional<float> oFloat = std::nullopt;
  // direct:
  std::optional<int> oInt(10);
  std::optional oIntDeduced(10); // deduction guides
  // make_optional
  auto oDouble = std::make_optional(3.0);
  auto oComplex = std::make_optional<std::complex<double>>(3.0, 4.0);
  // in_place
  std::optional<std::complex<double>> o7{std::in_place, 3.0, 4.0};
  // will call vector with direct init of {1, 2, 3}
  std::optional<std::vector<int>> oVec(std::in_place, {1, 2, 3});
  // copy from other optional:
  auto oIntCopy = oInt;
  // default constructor
  std::optional<UserName> opt{std::in_place};
  ```

  注意，当有以下情况时更推荐使用`std::in_place`（或使用`std::make_optional()`）
  - **默认构造**，当不使用`std::in_place`时总是构造出空的`std::optional`而不是默认对象
  - **不具有copy/move构造**的对象可以采用`std::in_place`原地构造
  - **需要大量构造参数**的对象采用`std::in_place`性能更好

- **返回对象**
  使用`std::optional`时需要特别注意返回对象的构造，采用统一初始化的方式会导致copy-elision失效：

  ```C++
  std::optional<std::string> CreateString() {
    std::string str {"Hello Super Awesome Long String"};
    return {str}; // this one will cause a copy
    return str;   // this one moves
  }
  ```

- **访问对象**
  - `operator*`，若为空对象则UB
  - `operator->`，若为空对象则UB
  - `value()`，若为空对象则抛出`std::bad_optional_access`
  - `value_or(defaultVal)`，若为空对象则返回`defaultVal`

- **比较**
  `std::nullopt`比任意有内容的`std::optional`都要小，均有内容的`std::optional`则基于内容比较
- **开销**
  由于需要簿记内容是否存在，因此其对象大小会大于原对象，存在额外内存开销，而性能开销并不大，并**不会发生动态内存分配（值语义）**
- **其他**
  需要特别注意当内容是指针时，由于`std::optional`的访问与指针非常相似，指针存放在`std::optional<T*>`时极易出错，避免这样的用法
