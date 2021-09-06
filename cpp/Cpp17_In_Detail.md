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
- 

## 5. Standard Attributes

`TODO`

## 6. `std::optional`

`TODO`

## 7. `std::variant`

`TODO`

## 8. `std::any`

`TODO`

## 9. `std::string_view`

`TODO`

## 10. String Conversions

`TODO`

## 11. Searchers & String Matching

`TODO`

## 12. Filesystem

`TODO`

## 13. Parallel STL Algorithms

`TODO`

## 14. Other Changes In The Library

`TODO`

## 15. Examples

`TODO`
