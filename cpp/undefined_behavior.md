# What Every C Programmer Should Know About Undefined Behavior

[original post by Chris Lattner](https://blog.llvm.org/2011/05/what-every-c-programmer-should-know.html)

## Introduction to Undefined Behavior

[John Regehr's Blog](https://blog.regehr.org/archives/213)

C/C++中存在的未定义行为UB是因为语言设计者希望C/C++成为一个极度高效的语言，而相应的例如Java等语言则通过牺牲一定性能来使得语言更加安全

对于编译器而言，达成高效率并没有特殊技巧，主要是通过：

- 主要算法设计实现的高效，包括寄存器分配、调度等算法
- 高性能的技巧并合理运用，包括循环展开等
- 冗余抽象的移除，包括临时对象的移除、内联等
- 不要犯错

## Advantages of Undefined Behavior in C

通过未定义行为来达成更高的性能通常是未定义行为的主要意义，其中例如：

- **使用未初始化的变量 Use of an Uninitialized Variable**
  通常可以采用编译器警告、静态/动态分析工具来发现这类UB，这类UB的主要意义在于**不要求变量一进入作用域就被值初始化，从而提升性能**；对于单一个变量影响有限，但是如果动态分配一个大内存空间也要求全部初始化则会非常影响性能
- **有符号数溢出 Signed Integer Overflow**
  有符号数在进行计算后发生溢出，此时结果是UB，例如`INT_MAX + 1 == INT_MIN`是不一定的，此类优化可以例如将`X+1 > X`直接替换成`true`、或是将`X*2/2`直接替换为`X`

  一个较大的优化点在于形如`for (i = 0; i <= N; ++i)`若`i`不可能溢出则确定会执行`N+1`次后可以采用一系列循环优化策略来修改这个循环，而若是强制规定`-fwrapv`溢出后根据**补码规则**，则会禁止各类优化的发生

  注意：**无符号数的计算，溢出时规定根据补码规则进行wrapping，因此不是未定义行为**
- **超量的位移 Oversized Shift Amounts**
  将`uint32_t`位移32或更多位是未定义的，这主要是由于底层不同的CPU硬件对位移的实现不同，假如规定超量位移的结果，则编译器需要发出额外的指令，可能导致这些位移操作代价更高
- **野指针解引用以及数组越界访问 Dereferences of Wild Pointers and Out of Bounds Array Accesses**
  这些情况是常见的错误，但是若由编译器引入检查，例如**每次采用下标访问数组都去检查是否存在越界会显著降低性能**，并且ABI也必须保证数组的长度信息始终在访问时可获得
- **空指针解引用 Dereferencing a NULL Pointer**
  **解引用空指针并不会导致trap**，从而同样允许了大量编译优化的可能，例如Java禁止了存在副作用的操作被重排序到一次可能为空的指针解引用（编译器无法通过上下文证明指针一定非空）上下
- **违背类型规则 Violating Type Rules**
  例如将一个`int*`强制转换为`float*`并且访问也是UB（C语言要求此类操作通过`memcpy`来实现），这种UB允许**基于类型的别名分析Type-Based Alias Analysis, TBAA**引入大量优化，从而显著提升性能，例如如下函数，编译器可以将之直接优化为`memset(P, 0, 40000);`：

  ```cpp
  float *P;
  void zero_array() {
    int i;
    for (i = 0; i < 1000; ++i) {
      P[i] = 0.0f;
    }
  }
  ```
  
  通过`-fno-strict-aliasing`可以手动禁止该UB，从而禁止了TBAA，此时编译器必须根据`zero_array`实现一次循环赋值，因为此时编译器必须假定可能存在下述用法改变了`P`的值：

  ```cpp
  int main() {
    P = (float*)&P; // Type-Based Alias Analysis, TBAA violation in zero_array
    zero_array();
  }
  ```

  这种类型滥用非常少见且容易出错，因此并没有规定这种用法的规则（从而成为一个UB）使得编译器可以展开更多的优化措施

其他还有诸如求值顺序例如`foo(i, ++i)`、多线程下数据竞争、违背`restrcti`、除0错误等UB

## Interacting Compiler Optimizations Lead to Surprising Results

现代编译器的优化器有非常多的优化路线，并且会以一定的顺序迭代式的处理代码实现优化，不同的编译器包含的优化器实现也不同，例如下述代码：

```cpp
void contains_null_check(int *P) {
  int dead = *P;
  if (P == 0)
    return;
  *P = 4;
}
```

- 假如编译器首先运行Dead Code Elimination，随后运行Redundant Null Check Elimination：

  ```cpp
  void contains_null_check(int *P) {
    // int dead = *P;    <- 1.dead code is eliminated
    if (P == 0)       // <- 2.not a redundant null check
      return;
    *P = 4;
  }
  ```

- 假如编译器首先运行Redundant Null Check Elimination，随后运行Dead Code Elimination：

  ```cpp
  void contains_null_check(int *P) {
    int dead = *P;    // <- 1.deference means the P cannot be NULL,
    if (false)        //      so this redundant null check is replaced by false
      return;
    *P = 4;
  }
  ```

  ```cpp
  void contains_null_check(int *P) {
    // int dead = *P; // <- 2.dead code is eliminated
    // if (false)
    //   return;
    *P = 4;           // when P is NULL, segementation fault here
  }
  ```

由于**解引用空指针的行为是未定义的**，因此上述两种编译器的优化策略完全有可能，而显然在第二种优化策略下就会导致程序可能出现崩溃

除了上述这种例子外，由于编译器在**执行完内联inline后可以在局部实现非常多的优化**，因此存在未定义行为的代码将出现多种可能的实际优化结果，并且**均符合标准**

## Undefined Behavior and Security

未定义行为同样影响了程序的安全性，下述程序通过使用`size > size+1`来检查整数溢出的情况（多的一字节长度用来存储终止符`\0`），而这段检查是未定义行为，同样可能被优化器直接移除（有符号数的溢出是未定义的），随后如果真的出现溢出，读入的数据就会超出`string`能够存放的长度，从而实现**缓冲区溢出攻击**

```cpp
void process_something(int size) {
  // Catch integer overflow.
  if (size > size+1)
    abort();
  ...
  // Error checking from this code elided.
  char *string = malloc(size+1);
  read(fd, string, size);
  string[size] = 0;
  do_something(string);
  free(string);
}
```

## Debugging Optimized Code May Not Make Any Sense

在优化后的代码中可能会出现意想不到的结果，例如下述代码中若忘记将`i`初始化，则由于**使用未初始化变量**属于未定义行为，优化器可以将整个`zero_array`函数直接丢弃：

```cpp
float *P;
void zero_array() {
  int i;
  for (i = 0; i < 1000; ++i) {  // if forget the i = 0
    P[i] = 0.0f;
  }
}
```

另一种场景如下，由于**采用空指针作为函数指针调用**属于未定义行为，优化器可以假定调用`FP()`时，一定已经调用了`set()`进行赋值，从而可以实现如下替换，这种情况在优化后的代码中不会出现崩溃，但是在debug中的代码就会出现崩溃：

```cpp
static void (*FP)() = 0;
static void impl() {
  printf("hello\n");
}
void set() {
  FP = impl;
}
void call() {
  FP();
}
```

```cpp
void set() {}
void call() {
  printf("hello\n");
}
```

## "Working" Code That Uses Undefined Behavior Can "Break" as the Compiler Evolves or Changes

由于未定义行为在某些优化之下可能得以正常工作，但是**随着编译器自身的变化，对未定义行为的处理也可能不同**，从而导致在某个编译器某个优化参数下可以运行的程序，未必可以在另一个编译器下运行

> We've seen many cases where applications that **"appear to be work"** suddenly break when a newer LLVM is used to build it, or when the application was moved from GCC to LLVM. While LLVM does occasionally have a bug or two itself

最经典的例子比如：

- 未初始化的变量在某个版本下会被编译器默认初始化为`0`，而后续的**寄存器分配策略**修改使得该变量与一个非`0`寄存器共享值
- 栈上数组溢出在某个版本下只会影响到一个不再使用的旧变量，而更新后开始影响到被使用中的变量的值，这往往是编译器改变了**打包栈上变量**的策略，或是基于生命周期的分析更激进的使得**生命周期不重叠的对象共享一部分栈**

核心在于所有**基于未定义行为的编译优化策略可能在任意新版本、新参数下修改行为**，从而影响程序

> huge bodies of C code are land mines just waiting to explode. This is even worse because...

## There is No Reliable Way to Determine if a Large Codebase Contains Undefined Behavior

使用[undefined behavior sanitizer](https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html)
