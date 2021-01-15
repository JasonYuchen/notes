# C++ Coroutines: Understanding operator co_await

[original post](https://lewissbaker.github.io/2017/11/17/understanding-operator-co-await)

## Compiler <-> Library interaction

Coroutines TS没有定义coroutine的行为，而实际上定义了一个通用的机制让用户可以通过实现一系列方法来自定义coroutine的行为

Coroutines定义了以下两种接口：

- **Promise接口**
    用于定义coroutine的行为自身，即当coroutine被调用到的时候以及返回的时候的行为
- **Awaitable接口**
    用于定义控制`co_await`行为的methods，当调用`co_await`时将会将调用翻译成一系列Awaitable对象的方法，例如是否需要暂停当前的协程，暂停执行后额外的逻辑，恢复执行前额外的逻辑等

## Awaiters and Awaitables: Explaining `operator co_await`

### 1. Obtaining the Awaiter

### 2. Awaiting the Awaiter

## Coroutine Handles

## Synchronisation-free async code

### 1. Comparison to Stackful Coroutines

## Avoiding memory allocations

## An example: Implementing a simple thread-synchronisation primitive

### 1. Defining the Awaiter

### 2. Filling out the rest of the event class

## Closing Off
