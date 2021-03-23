# Designing a Userspace Disk I/O Scheduler for Modern Datastores

## 动机与原理 Motivation

在诸如数据库等应用中，会有大量需要进行磁盘读写的线程，称之为Actor（即并发编程模型中的Actor），而与网络I/O不同的是磁盘I/O通常很难进行**Actor之间的IO申请调度、带宽分配、优先级等细粒度的控制**，例如如果一次较短的读取请求被排在较多的写请求后执行，则读请求的延迟就会显著增大

### 限制在途的I/O请求数 Limiting the number of outstanding requests

在高并发的场景下，我们通常希望产生的I/O请求能够迅速被底层存储层处理结束，但是现实中往往会出现I/O请求的堆积，而**堆积的位置在I/O栈中有多种可能**：磁盘队列、内核阻塞层、文件系统等

对于seastar这样的异步模型，请求在队列中堆积而最终会完成（无论成功失败）并不影响异步模型，并且延迟是不能免除的，只是采用用户态的调度器进行所有请求的精细控制，其真正优点如下：

- **统计数据 metrics**：在用户态控制所有请求可以统计出在I/O不同阶段下等待的请求，从而对于上层负载均衡、限流器、熔断器等组件可以对全局的请求有更好的把握，即加强了系统的**可观测性observability**
- **优先级 priority**：有一些特定的请求可以避免被排在无法控制的底层队列尾部，而是可以在有机会时直接执行
- **撤销 cancel**：对于一些还未执行的请求可以及时撤销，从而尽可能避免浪费硬件资源

![ioscheduler1](images/ioscheduler1.png)

### 文件系统 The filesystem

### 阻塞层 The Linux block layer

### 磁盘内队列 The disk array itself

### 限制磁盘并行度 Limiting the disk parallelism in practice

## 调度器的设计 Seastar Disk I/O Scheduler Design - The I/O Queues

### 选择队列数量 Choosing the right number of I/O Queues

[This option is deprecated](https://github.com/scylladb/seastar/issues/864)

### 优先级 Priority classes in Scylla

### 队列的内部功能 Internal Functioning of an I/O Queue

### Scylla的调度器 Scylla's I/O Scheduler in Practice

### 整合所有设计 Putting it all together

## 总结 Summary and future work
