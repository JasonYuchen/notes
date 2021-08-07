# Comprehensive Tutorial

[original](https://github.com/scylladb/seastar/blob/master/doc/tutorial.md)

## 简介 Introduction

[简介](Introduction.md)

## 初步 Getting started

[编译与测试](Setup.md)

## 线程和内存 Threads and memory

1. **线程 Seastar threads**
   在程序中使用`seastar::smp::count`来获知运行的线程数，并且在命令行中使用`-c<num>`来传入希望使用的线程数`<num>`，例如在双核四线程的机器上可以使用`-c4`，而当指定的线程数超过CPU虚拟内核数量时会报错
2. **内存 Seastar memory**
   seastar根据运行的线程数预先均分可用的内存量，并且在该线程shard内的分配（`malloc()`或`new`）都会只使用这块内存，可以在命令行使用`--reserve-memory`指定保留给OS的内存数量，或使用`-m<num><unit>`来指定给seastar使用的内存数量，单位`<unit>`可以是`k/M/G/T`，例如`-m10T`意味着允许seastar使用10T内存，超过实际物理内存量就会报错

## Inroducting futures and continuations

`TODO`

当计算完成时可以返回`seastar::make_ready_future<T>(...)`，此时对应的`.then()`就会被优化，直接调用而不是等到下一次循环，但是为了避免连续的`.then()`导致其他任务、事件循环饥饿starvation，会在连续运行256（默认值，无法通过配置修改）个任务时被抢占，后续任务会留到下一次事件循环返回时执行

## 协程 Coroutines

[协程](Coroutines.md)

## 连续 Continuations

`TODO`

## 处理异常 Handling exceptions

`TODO`

协程中的异常处理[见此](Coroutines.md#协程中的异常处理)

## 生命期管理 Lifetime management

## 高级特性 Advanced futures

## 纤程 Fibers

## 循环 Loops

## 等待多个事件 `when_all`：Waiting for multiple futures

## 信号 Semaphores

## 管道 Pipes

## 停止服务 Shutting down a service with a gate

## 无共享 Introducing shared-nothing programming

## 事件循环 More about Seastar's event loop

## 网络栈 Introducing Seastar's network stack

## 分片服务 Sharded services

## 干净的停止服务 Shutting down cleanly

## 命令行选项 Command line options

## 查错 Debugging a Seastar program

## Promise objects

## 内存分配器 Memory allocation in Seastar

## `Seastar::thread`

## 组件隔离 Isolation of application components
