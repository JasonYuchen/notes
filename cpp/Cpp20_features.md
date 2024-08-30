# All C++20 Core Language Features with Examples (partial)

[origin post](https://oleksandrkvl.github.io/2021/04/02/cpp-20-overview.html)

## Concepts

### Require expression

```cpp
template<typename T> /*...*/
requires (T x) // optional set of fictional parameter(s)
{
    // simple requirement: expression must be valid
    x++;    // expression must be valid
    
    // type requirement: `typename T`, T type must be a valid type
    typename T::value_type;
    typename S<T>;

    // compound requirement: {expression}[noexcept][-> Concept];
    // {expression} -> Concept<A1, A2, ...> is equivalent to
    // requires Concept<decltype((expression)), A1, A2, ...>
    {*x};  // dereference must be valid
    {*x} noexcept;  // dereference must be noexcept
    // dereference must  return T::value_type
    {*x} noexcept -> std::same_as<typename T::value_type>;
    
    // nested requirement: requires ConceptName<...>;
    requires Addable<T>; // constraint Addable<T> must be satisfied
};
```

### Concept

```cpp
template<typename T>
concept Addable = requires(T a, T b)
{
    a + b;
};

template<typename T>
concept Dividable = requires(T a, T b)
{
    a/b;
};

template<typename T>
concept DivAddable = Addable<T> && Dividable<T>;

template<typename T>
void f(T x)
{
    if constexpr(Addable<T>){ /*...*/ }
    else if constexpr(requires(T a, T b) { a + b; }){ /*...*/ }
}
```

### Requires clause

```cpp
// right after template<> block
template<typename T>
requires Addable<T>
auto f1(T a, T b); // Addable<T>

// last element of a function declaration
template<typename T>
auto f1(T a, T b) requires Subtractable<T>; // Subtractable<T>

// at both places at once
template<typename T>
requires Addable<T>
auto f1(T a, T b) requires Subtractable<T>; // Addable<T> && Subtractable<T>

// lambda included
auto l = []<typename T> requires Addable<T>
    (T a, T b) requires Subtractable<T>{};

template<typename T>
requires Addable<T>
class C;

// infamous `requires requires`. First `requires` is requires-clause,
// second one is requires-expression. Useful if you don't want to introduce new
// concept.
template<typename T>
requires requires(T a, T b) {a + b;}
auto f4(T x);

// use concept name instead of typename
template<Addable T>
void f();
```

```cpp
template<typename T>
concept Integral = std::integral<T>;

template<typename T>
concept Integral4 = std::integral<T> && sizeof(T) == 4;

// requires-clause also works here
template<template<typename T1> requires Integral<T1> typename T>
void f2(){}

// f() and f2() forms are equal
template<template<Integral T1> typename T>
void f(){
    f2<T>();
}

// unconstrained template template parameter can accept constrained arguments
template<template<typename T1> typename T>
void f3(){}

template<typename T>
struct S1{};

template<Integral T>
struct S2{};

template<Integral4 T>
struct S3{};

void test(){
    f<S1>();    // OK
    f<S2>();    // OK
    // error, S3 is constrained by Integral4 which is more constrained than
    // f()'s Integral
    f<S3>();

    // all are OK
    f3<S1>();
    f3<S2>();
    f3<S3>();
}

template<typename T>
struct X{
    void f() requires std::integral<T>
    {}
};

// Functions with unsatisfied constraints become invisible
void f(){
    X<double> x;
    x.f();  // error
    auto pf = &X<double>::f;    // error
}
```

### Constrained `auto`

```cpp
template<typename T>
concept is_sortable = true;

auto l = [](auto x){};
void f1(auto x){}               // unconstrained template
void f2(is_sortable auto x){}   // constrained template

template<is_sortable auto NonTypeParameter, is_sortable TypeParameter>
is_sortable auto f3(is_sortable auto x, auto y)
{
    // notice that nothing is allowed between constraint name and `auto`
    is_sortable auto z = 0;
    return 0;
}

template<is_sortable auto... NonTypePack, is_sortable... TypePack>
void f4(TypePack... args){}

int f();

// takes two parameters
template<typename T1, typename T2>
concept C = true;
// binds second parameter
C<double> auto v = f(); // means C<int, double>

struct X{
    operator is_sortable auto() {
        return 0;
    }
};

auto f5() -> is_sortable decltype(auto){
    f4<1,2,3>(1,2,3);
    return new is_sortable auto(1);
}
```

### Partial ordering by constraints

`C1 && C2` is more constrained than `C1`, `C1` is more constrained than `C1 || C2`, `requires-expression` is not further decomposed

```cpp
template<typename T>
concept integral_or_floating = std::integral<T> || std::floating_point<T>;

template<typename T>
concept integral_and_char = std::integral<T> && std::same_as<T, char>;

void f(std::integral auto){}        // #1
void f(integral_or_floating auto){} // #2
void f(std::same_as<char> auto){}   // #3

// calls #1 because std::integral is more constrained
// than integral_or_floating(#2)
f(int{});
// calls #2 because it's the only one whose constraint is satisfied
f(double{});
// error, #1, #2 and #3's constraints are satisfied but unordered
// because std::same_as<char> appears only in #3
f(char{});

void f(integral_and_char auto){}    // #4

// calls #4 because integral_and_char is more
// constrained than std::same_as<char>(#3) and std::integral(#1)
f(char{});
```

### Conditionally trivial special member functions

```cpp
template<typename T>
class optional{
public:
    optional() = default;

    // trivial copy-constructor
    optional(const optional&) = default;

    // non-trivial copy-constructor
    optional(const optional& rhs)
        requires(!std::is_trivially_copy_constructible_v<T>){
        // ...
    }

    // trivial destructor
    ~optional() = default;

    // non-trivial destructor
    ~optional() requires(!std::is_trivial_v<T>){
        // ...
    }
    // ...
private:
    T value;
};

// choose the trivial copy-ctor and trivial dtor
static_assert(std::is_trivial_v<optional<int>>);

// choose the non-trivial copy-ctor and non-trivial dtor
static_assert(!std::is_trivial_v<optional<std::string>>);
```

## Modules

- macro-unfriendly, can't pass manual macros to modules
- cannot have cyclic dependencies, must be a self-contained entity

```cpp
// module.cpp
// dots in module name are for readability purpose, they have no special meaning
export module my.tool;  // module declaration

export void f(){}       // export f()
void g(){}              // but not g()

// client.cpp
import my.tool;

f();    // OK
g();    // error, not exported
```

### Module units

- multiple implementation units (`module tool;`) are allowed
- importing other module's partitions are disallowed
- interface partitions must be re-exported by the module via `export import`

```cpp
// tool.cpp
export module tool; // primary module interface unit
export import :helpers; // re-export(see below) helpers partition

export void f();
export void g();

// tool.internals.cpp
module tool:internals;  // implementation partition
void utility();

// tool.impl.cpp
module tool;    // implementation unit, implicitly imports primary module unit
import :internals;

void utility(){}

void f(){
    utility();
}

// tool.impl2.cpp
module tool;    // another implementation unit
void g(){}

// tool.helpers.cpp
export module tool:helpers; // module interface partition
import :internals;

export void h(){
    utility();
}

// client.cpp
import tool;

f();
g();
h();
```

### Export

```cpp
// tool.cpp
module tool;
export import :helpers; // import and re-export helpers interface partition

export int x{}; // export single declaration

export{         // export multiple declarations
    int y{};
    void f(){};
}

export namespace A{ // export the whole namespace
    void f();
    void g();
}

namespace B{
    export void f();// export a single declaration within a namespace
    void g();
}

namespace{
    export int x;   // error, x has internal linkage
    export void f();// error, f() has internal linkage
}

export class C; // export as incomplete type
class C{};
export C get_c();

// client.cpp
import tool;

C c1;    // error, C is incomplete
auto c2 = get_c();  // OK
```

### Import

```cpp
// tool.cpp
export module tool;
import :helpers;  // import helpers partition

export void f(){}

// tool.helpers.cpp
export module tool:helpers;

export void g(){}

// client.cpp
import tool;

f();
g();
```

- special `import` form that allows import of importable headres: `import <header.h>`, compiler creates a synthesized header unit and make all declarations implicitly exported

### Global module fragment

```cpp
// header.h
#pragma once
class A{};
void g(){}

// tool.cpp
module;             // global module fragment
#include "header.h" // use old-school headers within a module
export module tool; // ends here

export void f(){    // uses declarations from header.h
    g();
    A a;
}
```

### Private module fragment

```cpp
export module tool; // interface

export void f();    // declared here

module :private;    // implementation details

void f(){}          // defined here
```

### No more implicit `inline`

```cpp
// header.h
struct C{
    void f(){}  // still inline because attached to a global module
};

// tool.cpp
module;
#include "header.h"

export module tool;

class A{};  // not exported

export struct B{// B is attached to module "tool"
    void f(){   // not implicitly inline anymore
        A a;    // can safely use non-exported name
    }

    inline void g(){
        A a;    // oops, uses non-exported name
    }

    inline void h(){
        f();    // fine, f() is not inline
    }
};

// client.cpp
import tool;

B b;
b.f();  // OK
b.g();  // error, A is undefined
b.h();  // OK
```

## Coroutines

see [coroutine series](../coroutine/Cppcoro_Coroutine_Theory.md)

## Three-way comparison

```cpp
template<typename T1, typename T2>
void TestComparisons(T1 a, T2 b)
{
    (a < b), (a <= b), (a > b), (a >= b), (a == b), (a != b);
}

struct S2
{
    int a;
    int b;
};

struct S1
{
    int x;
    int y;
    // support homogeneous comparisons
    auto operator<=>(const S1&) const = default;
    // this is required because there's operator==(const S2&) which prevents
    // implicit declaration of defaulted operator==()
    bool operator==(const S1&) const = default;

    // support heterogeneous comparisons
    std::strong_ordering operator<=>(const S2& other) const
    {
        if (auto cmp = x <=> other.a; cmp != 0)
            return cmp;
        return y <=> other.b;
    }

    bool operator==(const S2& other) const
    {
        return (*this <=> other) == 0;
    }
};

TestComparisons(S1{}, S1{});
TestComparisons(S1{}, S2{});
TestComparisons(S2{}, S1{});
```

### Legacy code

```cpp
// not in our immediate control
struct Legacy
{
    bool operator==(Legacy const&) const;
    bool operator<(Legacy const&) const;
};

struct S6
{
    int x;
    Legacy l;
    // deleted because Legacy doesn't have operator<=>(), comparison category
    // can't be deduced
    auto operator<=>(const S6&) const = default;
};

struct S7
{
    int x;
    Legacy l;

    std::strong_ordering operator<=>(const S7& rhs) const = default;
    /*
    Since comparison category is provided explicitly, ordering can be
    synthesized using operator<() and operator==(). They must return exactly
    `bool` for this to work. It will work for weak and partial ordering as well.
    
    Here's an example of synthesized operator<=>():
    std::strong_ordering operator<=>(const S7& rhs) const
    {
        // use operator<=>() for int
        if(auto cmp = x <=> rhs.x; cmp != 0) return cmp;

        // synthesize ordering for Legacy using operator<() and operator==()
        if(l == rhs.l) return std::strong_ordering::equal;
        if(l < rhs.l) return std::strong_ordering::less;
        return std::strong_ordering::greater;
    }
    */
};

struct NoEqual
{
    bool operator<(const NoEqual&) const = default;
};

struct S8
{
    NoEqual n;
    // deleted, NoEqual doesn't have operator<=>()
    // auto operator<=>(const S8&) const = default;

    // deleted as well because NoEqual doesn't have operator==()
    std::strong_ordering operator<=>(const S8&) const = default;
};

struct W
{
    std::weak_ordering operator<=>(const W&) const = default;
};

struct S9
{
    W w;
    // ask for strong_ordering but W can provide only weak_ordering, this will
    // yield an error during instantiation
    std::strong_ordering operator<=>(const S9&) const = default;
    void f()
    {
        (S9{} <=> S9{});    // error
    }
};
```

## Lambda

#### Allow lambda-capture

```cpp
struct S{
    void f(){
        [=]{};          // captures this by reference, deprecated since C++20
        [=, *this]{};   // OK since C++17, captures this by value
        [=, this]{};    // OK since C++20, captures this by reference
    }
};
```

### Template parameter list for generic lambdas

```cpp
// lambda that expect std::vector<T>
// until C++20:
[](auto vector){
    using T =typename decltype(vector)::value_type;
    // use T
};
// since C++20:
[]<typename T>(std::vector<T> vector){
    // use T
};

// access argument type
// until C++20
[](const auto& x){
    using T = std::decay_t<decltype(x)>;
    // using T = decltype(x); // without decay_t<> it would be const T&, so
    T copy = x;               // copy would be a reference type
    T::static_function();     // and these wouldn't work at all
    using Iterator = typename T::iterator;
};
// since C++20
[]<typename T>(const T& x){
    T copy = x;
    T::static_function();
    using Iterator = typename T::iterator;
};

// perfect forwarding
// until C++20:
[](auto&&... args){
    return f(std::forward<decltype(args)>(args)...);
};
// since C++20:
[]<typename... Ts>(Ts&&... args){
    return f(std::forward<Ts>(args)...);
};

// and of course you can mix them with auto-parameters
[]<typename T>(const T& a, auto b){};
```

### Pack expansion in lambda init-capture

```cpp
void g(int, int){}

// C++17
template<class F, class... Args>
auto delay_apply(F&& f, Args&&... args) {
    return [f=std::forward<F>(f), tup=std::make_tuple(std::forward<Args>(args)...)]()
            -> decltype(auto) {
        return std::apply(f, tup);
    };
}

// C++20
template<typename F, typename... Args>
auto delay_call(F&& f, Args&&... args) {
    return [f = std::forward<F>(f), ...f_args=std::forward<Args>(args)]()
            -> decltype(auto) {
        return f(f_args...);
    };
}

void f(){
    delay_call(g, 1, 2)();
}
```

## Constant expressions

### Immediate functions `consteval`

```cpp
consteval int GetInt(int x){
    return x;
}

constexpr void f(){
    auto x1 = GetInt(1);
    constexpr auto x2 = GetInt(x1); // error x1 is not a constant-expression
}
```

### `constexpr` virtual function

```cpp
struct Base{
    constexpr virtual ~Base() = default;
    virtual int Get() const = 0;    // non-constexpr
};

struct Derived1 : Base{
    constexpr int Get() const override {
        return 1;
    }
};

struct Derived2 : Base{
    constexpr int Get() const override {
        return 2;
    }
};

constexpr auto GetSum(){
    const Derived1 d1;
    const Derived2 d2;
    const Base* pb1 = &d1;
    const Base* pb2 = &d2;

    return pb1->Get() + pb2->Get();
}

static_assert(GetSum() == 1 + 2);   // evaluated at compile-time
```

### `constexpr` allocations

```cpp
constexpr auto get_str()
{
    std::string s1{"hello "};
    std::string s2{"world"};
    std::string s3 = s1 + s2;
    return s3;
}

constexpr auto get_array()
{
    constexpr auto N = get_str().size();
    std::array<char, N> arr{};
    std::copy_n(get_str().data(), N, std::begin(arr));
    return arr;
}

static_assert(!get_str().empty());

// error because it holds data allocated at compile-time
constexpr auto str = get_str();

// OK, string is stored in std::array<char>
constexpr auto result = get_array();
```

### Trivial default initialization in `constexpr` functions

```cpp
struct NonTrivial{
    bool b = false;
};

struct Trivial{
    bool b;
};

template <typename T>
constexpr T f1(const T& other) {
    T t;        // default initialization
    t = other;
    return t;
}

template <typename T>
constexpr auto f2(const T& other) {
    T t;
    return t.b;
}

void test(){
    constexpr auto a = f1(Trivial{});   // error in C++17, OK in C++20
    constexpr auto b = f1(NonTrivial{});// OK

    constexpr auto c = f2(Trivial{}); // error, uninitialized Trivial::b is used
    constexpr auto d = f2(NonTrivial{}); // OK
}
```

## Aggregates

### Prohibit aggregates with user-declared constructors

```cpp
// none of the types below are an aggregate in C++20
struct S{
    int x{2};
    S(int) = delete; // user-declared ctor
};

struct X{
    int x;
    X() = default;  // user-declared ctor
};

struct Y{
    int x;
    Y();            // user-provided ctor
};

Y::Y() = default;

void f(){
    S s(1);     // always an error
    S s2{1};    // OK in C++17, error in C++20, S is not an aggregate now
    X x{1};     // OK in C++17, error in C++20
    Y y{2};     // always an error
}
```

### Class template argument deduction for aggregates

```cpp
template<typename T, typename U>
struct S{
    T t;
    U u;
};
// deduction guide was needed in C++17
// template<typename T, typename U>
// S(T, U) -> S<T,U>;

S s{1, 2.0};    // S<int, double>

template<typename T>
struct MyData{
    T data;
};
MyData(const char*) -> MyData<std::string>;

MyData s1{"abc"};   // OK, MyData<std::string> using deduction guide
MyData<int> s2{1};  // OK, explicit template argument
MyData s3{1};       // Error, CTAD isn't involved

// work with pack expansions
template<typename... Ts>
struct Overload : Ts...{
    using Ts::operator()...;
};
// no need for deduction guide anymore

Overload p{[](int){
        std::cout << "called with int";
    }, [](char){
        std::cout << "called with char";
    }
};     // Overload<lambda(int), lambda(char)>
p(1);   // called with int
p('c'); // called with char
```

### Parenthesized initialization of aggregates

```cpp
struct S{
    int a;
    int b = 2;
    struct S2{
        int d;
    } c;
};

struct Ref{
    const int& r;
};

int GetInt(){
    return 21;
}

S{0.1}; // error, narrowing
S(0.1); // OK

S{.a=1}; // OK
S(.a=1); // error, no designated initializers

Ref r1{GetInt()}; // OK, lifetime is extended
Ref r2(GetInt()); // dangling, lifetime is not extended

S{1, 2, 3}; // OK, brace elision, same as S{1,2,{3}}
S(1, 2, 3); // error, no brace elision

// values without initializers take default values or value-initialized(T{})
S{1}; // {1, 2, 0}
S(1); // {1, 2, 0}

// make_unique works now
auto ps = std::make_unique<S>(1, 2, S::S2{3});

// arrays are also supported
int arr1[](1, 2, 3);
int arr2[2](1); // {1, 0}
```

## Structured bindings

### Lambda capture and storage class specifiers for structured bindings

```cpp
struct S{
    int a: 1;
    int b: 1;
    int c;
};

static auto [A,B,C] = S{};

void f(){
    [[maybe_unused]] thread_local auto [a,b,c] = S{};
    auto l = [=](){
        return a + b + c;
    };

    auto m = [&](){
        // error, can't capture bit-fields 'a' and 'b' by-reference
        // return a + b + c;
        return c;
    };
}
```

### Allow structured bindings to accessible members

```cpp
struct A {
    friend void foo();
private:
    int i;
};

void foo() {
    A a;
    auto x = a.i;   // OK
    auto [y] = a;   // Ill-formed until C++20, now OK
}
```

## Range-based loop

### init-stmt for range-based for-loop

```cpp
class Obj{
    std::vector<int>& GetCollection();
};

Obj GetObj();

// dangling reference, lifetime of Obj return by GetObj() is not extended
for(auto x : GetObj().GetCollection()){
    // ...
}

// OK
for(auto obj = GetObj(); auto item : obj.GetCollection()){
    // ...
}

// also can be used to maintain index
for(std::size_t i = 0; auto& v : collection){
    // use v...
    i++;
}
```

## Attributes

### no unique address

```cpp
struct Empty{};

template<typename T>
struct Cpp17Widget{
    int i;
    T t;
};

template<typename T>
struct Cpp20Widget{
    int i;
    [[no_unique_address]] T t;
};

static_assert(sizeof(Cpp17Widget<Empty>) > sizeof(int));
static_assert(sizeof(Cpp20Widget<Empty>) == sizeof(int));
```

## Character encoding

```cpp
void HandleString(const char*){}
// distinct function name is required to handle UTF-8 in C++17
void HandleStringUTF8(const char*){}
// now it can be done using convenient overload
void HandleString(const char8_t*){}

void Cpp17(){
    HandleString("abc");        // char[4]
    HandleStringUTF8(u8"abc");  // C++17: char[4] but UTF-8, 
                                // C++20: error, type is char8_t[4]
}

void Cpp20(){
    HandleString("abc");    // char
    HandleString(u8"abc");  // char8_t
}
```

## Sugar

### Designated initializers

```cpp
struct S{
    int x;
    int y{2};
    std::string s;
};
S s1{.y = 3};   // {0, 3, {}}
S s2 = {.x = 1, .s = "abc"};    // {1, 2, {"abc"}}
S s3{.y = 1, .x = 2};   // Error, x should be initialized before y
```

### Default member initializers for bit-fields

```cpp
// until C++20:
struct S{
    int a : 1;
    int b : 1;
    S() : a{0}, b{1}{}
};

// since C++20:
struct S{
    int a : 1 {0},
    int b : 1 = 1;
};
```

### Nested `inline` namespaces

```cpp
// C++20
namespace A::B::inline C{
    void f(){}
}
// C++17
namespace A::B{
    inline namespace C{
        void f(){}
    }
}
```

### `using enum`

```cpp
namespace my_lib {
enum class color { red, green, blue };
enum COLOR {RED, GREEN, BLUE};
enum class side {left, right};
}

void f(my_lib::color c1, my_lib::COLOR c2){
    using enum my_lib::color;   // introduce scoped enum
    using enum my_lib::COLOR;   // introduce unscoped enum
    using my_lib::side::left;   // introduce single enumerator id

    // C++17
    if(c1 == my_lib::color::red){/*...*/}
    
    // C++20
    if(c1 == green){/*...*/}
    if(c2 == BLUE){/*...*/}

    auto r = my_lib::side::right;   // qualified id is required for `right`
    auto l = left;                  // but not for `left`
}
```

### Array size deduction in new-expressions

```cpp
// before C++20
int p0[]{1, 2, 3};
int* p1 = new int[3]{1, 2, 3};  // explicit size is required

// since C++20
int* p2 = new int[]{1, 2, 3};
int* p3 = new int[]{};  // empty
char* p4 = new char[]{"hi"};
// works with parenthesized initialization of aggregates
int p5[](1, 2, 3);
int* p6 = new int[](1, 2, 3);
```

## Others

### `constinit`

- enforces that variable is initialized at compile-time

```cpp
struct S {
    constexpr S(int) {}
    ~S(){}; // non-trivial
};

constinit S s1{42};  // OK
constexpr S s2{42};  // error because destructor is not trivial

// tls_definitions.cpp
thread_local constinit int tls1{1};
thread_local int tls2{2};

// main.cpp
extern thread_local constinit int tls1;
extern thread_local int tls2;

int get_tls1() {
    return tls1;  // pure TLS access
}

int get_tls2() {
    return tls2;  // has implicit TLS initialization code
}
```

### Implicit move for more local objects and rvalue references

```cpp
std::unique_ptr<T> f0(std::unique_ptr<T> && ptr) {
    return ptr; // copied in C++17(thus, error), moved in C++20, OK
}

std::string f1(std::string && x) {
    return x;   // copied in C++17, moved in C++20
}

struct Widget{};

void f2(Widget w){
    throw w;    // copied in C++17, moved in C++20
}

struct From {
    From(Widget const &);
    From(Widget&&);
};

struct To {
    operator Widget() const &;
    operator Widget() &&;
};

From f3() {
    Widget w;
    return w;  // moved (no NRVO because of different types)
}

Widget f4() {
    To t;
    return t;// copied in C++17(conversions were not considered), moved in C++20
}

struct A{
    A(const Widget&);
    A(Widget&&);
};

struct B{
    B(Widget);
};

A f5() {
    Widget w;
    return w;  // moved
}

B f6() {
    Widget w;
    return w; // copied in C++17(because there's no B(Widget&&)), moved in C++20
}

struct Derived : Widget{};

std::shared_ptr<Widget> f7() {
    std::shared_ptr<Derived> result;
    return result;  // moved
}

Widget f8() {
    Derived result;
    // copied in C++17(because there's no Base(Derived)), moved in C++20
    return result;
}
```
