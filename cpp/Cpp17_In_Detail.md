# C++17 In Detail

Only part of the changes are noted here.

## 1. Fixes and Depreaction

- **针对直接列表初始化的新`auto`推导规则**

  - 对于大括号直接初始化仅包含单个元素的，从该元素推导（而不会像C++11中推导为`std::initializer_list<T>`）
  - 对于大括号直接初始化包含多个元素的，无法推导类型

  ```cpp
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

      ```cpp
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
  
  ```cpp
  class X { int a; };
  X{10} // this expression is prvalue
  X x;  // x is lvalue
  x.a   // it's lvalue (location)
  ```

  > **prvalues** perform initialisation, **glvalues** describe locations

- **分配对象时内存对齐**

  ```cpp
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

  ```cpp
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
  
  ```cpp
  struct MyClass {
    inline static int sValue = 999;
  };
  ```

## 4. Templates

- **类模板参数推导 Template Argument Deduction for Class Templates**
  模板函数的模板参数可以根据函数参数自动推导，这种方式拓展到了类模板，模板类的模板参数可以根据构造函数的参数自动推导
  
  ```cpp
  using namespace std::string_literals;
  std::pair myPair(42, "hellow world"s); // std::pair<int, std::string>
  std::array arr{1, 2, 3};               // std::array<int, 3>
  ```

  编译器通过**推导规则Deduction Guides**来尝试推导模板类的模板参数，推导规则包含编译器隐式生成规则和用户自定义规则两类：

  ```cpp
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

  ```cpp
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

  ```cpp
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

  ```cpp
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
  
  ```cpp
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

  ```cpp
  static void impl1() {...} // compilers may warn about this
  [[maybe_unused]] static void impl2() {...} // suppressed

  void foo() {
    int x = 42; // compilers may warn about this
    [[maybe_unsed]] int y = 53; // suppressed
  }
  ```

- `[[nodiscard]]`

  ```cpp
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

  ```cpp
  namespace [[deprecated("use BetterUtils")]] GoodUtils {
    void doStuff();  // when used, warning: 'GoodUtils' is deprecated: use BetterUtils
  }
  ```

- `[[noreturn]]`
- `[[carries_dependency]]`

## 6. `std::optional`

- **创建对象**
  
  ```cpp
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

  ```cpp
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

## 7. `std::variant`

类型安全的`union`

- **创建对象**
  默认情况下`std::variant`会根据模板参数第一个类型进行默认初始化，假如第一个类型没有默认构造函数，可以传入`std::monostate`

  ```cpp
  // default initialisation: (the first type has to have a default ctor)
  std::variant<int, float> intFloat;
  // for first type does not have a default ctor, use monostate
  std::variant<std::monostate, NotSimple, int> okInit;
  // pass a value:
  std::variant<int, float, std::string> intFloatString { 10.5f };
  // ambiguity resolved by in_place (otherwise the 7.6 can be either long or float)
  variant<long, float, std::string> longFloatString {
    std::in_place_index<1>, 7.6 // double!
  };
  // ambiguity resolved by in_place
  std::variant<int, float> intFloat { std::in_place_type<int>, 10.5f };
  // in_place for complex types
  std::variant<std::vector<int>, std::string> vecStr {
    std::in_place_index<0>, { 0, 1, 2, 3 }
  };
  ```

  注意，与`std::optional`类似，`std::variant`通过`std::in_place_type<T>`和`std::in_place_index<>`来提供更有效的对象创建
- **生命周期**
  采用`union`需要自行管理生命周期，调用构造函数和析构函数等，而采用`std::variant`则会自动管理对象生命周期，当内部存储的值被修改时就会调用原对象的析构函数
- **访问对象**
  - 采用`std::get<index>`或`std::get<type>`来访问存储的对象，假如不匹配时就会抛出`std::bad_variant_access`异常
  - 采用`std::get_if<index>`或`std::get_if<type>`来访问存储的对象（注意传入的是`std::variant`的指针），假如不匹配时会返回`nullptr`
  - 采用`std::visit`来访问，参考[静态多态](https://github.com/JasonYuchen/notes/blob/master/cpp/polymorphism.md#%E9%B8%AD%E5%AD%90%E7%B1%BB%E5%9E%8B%E4%B8%8Estdvariant)
  - 当有多个`std::variant`需要访问时，可以只提供部分有效组合的重载，并**由generic lambda来处理其余情况**

    ```cpp
    std::variant<Pizza, Chocolate, Salami, IceCream> firstIngredient { IceCream() };
    std::variant<Pizza, Chocolate, Salami, IceCream> secondIngredient { Chocolate()};
    std::visit(overload{
      [](const Pizza& p, const Salami& s) {
        std::cout << "here you have, Pizza with Salami!\n";
      },
      [](const Salami& s, const Pizza& p) {
        std::cout << "here you have, Pizza with Salami!\n";
      },
      [](const auto& a, const auto& b) {
        std::cout << "invalid composition...\n";
      },
    }, firstIngredient, secondIngredient);
    ```

- **开销**
  与`union`类似，`std::variant`也基于可能包含最大的对象的空间，并额外加上簿记信息，同时也**不会发生动态内存分配（值语义）**
- **实例：状态机**
  
  ```cpp
  struct DoorState {
    struct DoorOpened {};
    struct DoorClosed {};
    struct DoorLocked {};
    using State = std::variant<DoorOpened, DoorClosed, DoorLocked>;
    State m_state;
    void open() { m_state = std::visit(OpenEvent{}, m_state); }
    void close() { m_state = std::visit(CloseEvent{}, m_state); }
    void lock() { m_state = std::visit(LockEvent{}, m_state); }
    void unlock() { m_state = std::visit(UnlockEvent{}, m_state); }
  };
  struct OpenEvent {
    State operator()(const DoorOpened&){ return DoorOpened(); }
    State operator()(const DoorClosed&){ return DoorOpened(); }
    // cannot open locked doors
    State operator()(const DoorLocked&){ return DoorLocked(); }
  };
  // other events are similar
  ```

## 8. `std::any`

安全的`void*`，`std::any`可以存储任意对象，并且对基本类型有额外优化（免去动态内存分配）

- **创建对象**

  ```cpp
  // default initialisation:
  std::any a;
  // initialisation with an object:
  std::any a2{10}; // int
  std::any a3{MyType{10, 11}};
  // in_place:
  std::any a4{std::in_place_type<MyType>, 10, 11};
  std::any a5{std::in_place_type<std::string>, "Hello World"};
  // make_any
  std::any a6 = std::make_any<std::string>{"Hello World"};
  ```

  注意，与`std::optional`类似，`std::any`通过`std::in_place_type<T>`来提供更有效的对象创建
- **生命周期**
  `std::any`也会自动管理对象的生命周期，当赋予新对象时就会调用旧对象的析构函数
- **访问对象**
  - 只读访问，返回对象的副本，并在类型不匹配时抛出`std::bad_any_cast`
  - 读写访问，返回对象的引用，并在类型不匹配时抛出`std::bad_any_cast`
  - 读写访问，返回对象的指针，并在类型不匹配时返回`nullptr`

  ```cpp
  std::any_cast<MyType&>(var).Print();
  std::any_cast<MyType&>(var).a = 11;  // read/write
  std::any_cast<MyType&>(var).Print();
  std::any_cast<int>(var);             // throw std::bad_any_cast
  int* p = std::any_cast<int>(&var);   // p is nullptr
  class MyType* pt = std::any_cast<MyType>(&var);
  pt->a = 12;                          // read/write
  ```

- **开销**
  由于`std::any`不是模板类而是类型消除，因此**需要动态内存分配**来保存放入的对象（类似类型安全的`std::unique_ptr<void>`），但是对于`int`等小对象，标准**推荐采用Small Buffer Optimisation进行优化**以避免内存分配，但这也会导致例如保存`char`类型虽然避免了分配但是实际会占用更多空间

## 9. `std::string_view`

- **创建对象**

  ```cpp
  // the whole string:
  const char* cstr = "Hello World";
  std::string_view sv1 { cstr };
  std::string_view sv2 { cstr, 5 }; // not null-terminated!
  // from string:
  std::string str = "Hello String";
  std::string_view sv3 = str;
  // ""sv literal
  using namespace std::literals;
  std::string_view sv4 = "Hello\0 Super World"sv;
  ```

- **特殊操作**
  - `remove_prefix`：移除`string_view`对象的前缀，但不改变被引用的字符串本身
  - `remove_suffix`：移除`string_view`对象的后缀，但不改变被引用的字符串本身
  - `starts_with (since C++ 20)`：判断前缀是否符合某个字符串
  - `ends_with (since C++ 20)`：判断后缀是否符合某个字符串
- **注意点**
  - `string_view`往往引用了原字符串中间的一段，且不保证以`'\0'`结尾，因此所有接受C风格字符串的接口都不应该使用`string_view`
  - 由于`string_view`只是引用了一段字符串，因此在使用时需要确保原字符串有效
  - `string_view`除了`copy`等少数操作以外的操作，都被标记为`constexpr`，因此可以在编译期操作
- **性能对比**
  函数参数不同类型，接收参数后再用于初始化一个`std::string`时，不同场景的开销如下（SSO即Small String Optimization）：

  |Input parameter|`const string&`|`string_view`|`string` and `move`|
  |:-|:-|:-|:-|
  |`const char*`|2 allocs|1 alloc|1 alloc + move|
  |`const char*` + SSO|2 copies|1 copy|2 copies|
  |lvalue|1 alloc|1 alloc|1 alloc + move|
  |lvalue + SSO|1 copy|1 copy|2 copies|
  |rvalue|1 alloc|1 alloc|2 moves|
  |rvalue + SSO|1 copy|1 copy|2 copies|

## 10. String Conversions

- **`from_chars`**
  - 当成功时，`from_chars_result::ptr`指向第一个不符合的字符位置或等于`last`，`from_chars_result::ec`为值初始化值
  - 当无效转换时，`from_chars_result::ptr`指向`first`，`from_chars_result::ec`为`std::errc::invalid_argument`
  - 当越界时，`from_chars_result::ptr`指向第一个不符合的字符位置，`from_chars_result::ec`为`std::errc::result_out_of_range`

  ```cpp
  struct from_chars_result {
    const char* ptr;
    std::errc ec;
  };

  enum class chars_format {
    scientific = /*unspecified*/,
    fixed  = /*unspecified*/,
    hex  = /*unspecified*/,
    general = fixed | scientific
  };

  // TYPE is integral
  std::from_chars_result from_chars(const char* first, const char* last, TYPE& value, int base = 10);

  // FLOAT_TYPE is floating
  std::from_chars_result from_chars(const char* first, const char* last, FLOAT_TYPE& value, std::chars_format fmt = std::chars_format::general);
  ```

- **`to_chars`**
  - 当成功时，`to_chars_result::ptr`指向`last + 1`，`to_chars_result::ec`为值初始化，注意末尾不会加上`'\0'`
  - 当出错时，`to_chars_result::ptr`指向`first`
  - 当越界时，`to_chars_result::ec`为`std::errc::value_too_large`

  ```cpp
  struct to_chars_result {
    char* ptr;
    std::errc ec;
  };

  // TYPE is integral
  std::to_chars_result to_chars(char* first, char* last, TYPE value, int base = 10);

  // FLOAT_TYPE is floating
  std::to_chars_result to_chars(char* first, char* last, FLOAT_TYPE value, std::chars_format fmt, int precision);
  ```

## 11. Searchers & String Matching

提供了**更高性能的字符串匹配算法**，可以在`std::search`时选择所采用的算法：

- `default_searcher`：简单算法，在C++17前均采用这种算法，时间复杂度`O(nm)`
- `boyer_moore_searcher`：完整版boyer moore算法，时间复杂度`best O(n/m) worst O(nm)`
- `boyer_moore_horspool_searcher`：简化版boyer moore算法，时间复杂度相同

```cpp
template<class ForwardIterator, class Searcher>
ForwardIterator search(ForwardIterator first, ForwardIterator last, const Searcher& searcher);

std::string testString = "Hello Super World";
std::string needle = "Super";
const auto it = std::search(
    begin(testString),
    end(testString),
    boyer_moore_searcher(begin(needle), end(needle));
```

## 12. Filesystem

`TODO`

## 13. Parallel STL Algorithms

`TODO`

## 14. Other Changes In The Library

- **`std::byte`**

  ```cpp
  constexpr std::byte b{1};
  constexpr std::byte c{255};
  static_assert(std::to_integer<int>(b) == 0x01);
  ```

- **Map和Set的提升**
  - **Splicing**
    将节点从基于树的容器（`std::map/std::set`）高效的移动至另一个容器且避免额外的内存开销

    ```cpp
    std::set<std::string> setNames;
    setNames.emplace("John");
    std::set<std::string> outSet;
    auto handle = setNames.extract("John");
    outSet.insert(std::move(handle));
    ```
  
  - **Emplace Enhancements for `map/unordered_map`**
    提供了新的方法来插入元素，`try_emplace()`以及`insert_or_assign()`，原先的`emplace()`会无论是否已经存在对象都将参数`move`走，因此往往需要首先`find()`判断是否存在，而原先的`operator[]`会无论是否存在都默认构造，且调用者无法得知在调用前是否存在
    - `try_emplace()`：若相应的key已经存在，在不会有任何影响也不会`move`参数，若不存在则等同于`emplace`
    - `insert_or_assign()`：不像`operator[]`依赖默认构造函数的存在，且会返回是否是inserted

    ```cpp
    std::map<std::string, std::string> m;
    m["Hello"] = "World";
    std::string s = "C++";
    m.emplace(std::make_pair("Hello", std::move(s)));
    // what happens with the string 's'? s is moved away
    std::cout << s << '\n';
    std::cout << m["Hello"] << '\n';
    s = "C++";
    m.try_emplace("Hello", std::move(s));
    // s is unchanged since "Hello" exists
    std::cout << s << '\n';
    std::cout << m["Hello"] << '\n';


    std::map<std::string, User> mapNicks;
    //mapNicks["John"] = User("John Doe"); // error: no default ctor for User()
    auto [iter, inserted] = mapNicks.insert_or_assign("John", User("John Doe"));
    if (inserted)
      std::cout << iter->first << " entry was inserted\n";
    else
      std::cout << iter->first << " entry was updated\n";
    ```

- **`emplace`类的操作会返回引用**
  
  ```cpp
  // since C++11 and until C++17 for std::vector
  template< class... Args >
  void emplace_back( Args&&... args );
  // since C++17 for std::vector
  template< class... Args >
  reference emplace_back( Args&&... args );

  // in C++11/14:
  stringVector.emplace_back("Hello");
  // emplace doesn't return anything, so back() needed
  stringVector.back().append(" World");

  // in C++17:
  stringVector.emplace_back("Hello").append(" World");
  ```

- **采样算法**

  ```cpp
  std::vector<int> v { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
  std::vector<int> out;
  std::sample(v.begin(), // range start
              v.end(), // range end
              std::back_inserter(out), // where to put it
              3, // number of elements to sample
              std::mt19937{std::random_device{}()});
  // out可能是{1, 4, 9}
  ```

- **新数学函数**
  - `std::gcd`：最大公约数Greatest Common Divisor
  - `std::lcm`：最小公倍数Least Common Multiple
  - `std::clamp(v, min, max)`：若`v > max`就返回`max`，若`v < min`就返回`min`，否则返回`v`
  - 其他诸多新数学函数定义在`<cmath>`头文件

- **非成员`std::size(), std::data(), std::empty()`函数**
- **更多的`constexpr`**
- **一次性获得多个锁`std::scoped_lock`**
- **多态分配器`std::pmr`**
