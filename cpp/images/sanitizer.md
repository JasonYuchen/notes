# Sanitizer

## Address Sanitizer

[clang page](https://clang.llvm.org/docs/AddressSanitizer.html)

`TODO: example`

## Memory Sanitizer

[clang page](https://clang.llvm.org/docs/MemorySanitizer.html)

`TODO: example`

## Thread Sanitizer

[clang page](https://clang.llvm.org/docs/ThreadSanitizer.html)

`TODO: example`

## Undefined Behavior Sanitizer

[clang page](https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html)

`TODO: example`

## Enable Sanitizers in CMake

```cmake
set(CMAKE_CXX_CLANG_TIDY "clang-tidy;-checks=*")

# -fno-omit-frame-pointer and -fno-optimize-sibling-calls are needed in various sanitizers
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O0 -g -fno-omit-frame-pointer -fno-optimize-sibling-calls")

# leak sanitizer is turned on by default
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")

# cannot be used with thread or address
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=memory")

# cannot be used with memory or address
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=thread") 

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=undefined")
```
