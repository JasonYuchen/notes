# Seastar - Userspace Operating System

[seastar.io](http://seastar.io)

## Shared-nothing design

更详细的介绍[Seastar: Shared-nothing Architecture](Shared_Nothing.md)

- **单核性能提升有限，多核成为主流**
- **现代存储网络设备快速发展**
  随着存储和网络设备的急速发展，**基于中断interrupt-driven**的设备已经受CPU的限制而无法达到设计IOPS
  
  例如高速NIC：一个2GHz主频的处理器处理在10GBps的网卡上1024bytes的包，每个包仅有1670个处理器时钟周期，而一次中断就会产生**上下文切换context switch**等微秒级的延迟，绕过内核并解放CPU的[DPDK](https://www.dpdk.org)应运而生

  例如高速SSD：RocksDB的IOPS已经受限于CPU而不是存储设备，[KVell的设计](https://github.com/JasonYuchen/notes/blob/master/papers/2019_SOSP_KVell.md)也证实了这一点

  seastar使用Linux AIO进行异步读写文件，后期会修改成[io_uring](https://github.com/JasonYuchen/notes/blob/master/linux/io_uring.md)，在设计上就支持DPDK并重新实现了TCP协议栈以完全绕开Linux kernel
- **无共享：避免跨核同步**
  当需要跨核同步时，**采用锁可能会导致竞争和等待，浪费CPU的时钟周期**，即使采用原子变量，在竞争激烈时一次原子变量修改也会导致微秒级的耗时（普通变量仅需纳秒级耗时），参考[bvar的设计原理](https://github.com/apache/incubator-brpc/blob/8199994e54fb3077625a1539b21d63d8e9e75ca0/docs/en/bvar.md)
  ![arch1](images/arch1.png)

  **seastar在每个核心上运行一个应用程序线程，通过基于无锁队列显式的消息传递**（当跨核心通信不可避免时），而没有任何其他诸如共享数据、锁等同步方式，从而尽可能避免**锁unscalable lock primitives**、**缓存抖动cache bouncing**等影响
- **消息传递**
  简单的消息传递方式如下，显式的将一个任务递交给另一个cpu进行处理

    ```cpp
    smp::submit_to(cpu, lambda);
    ```

  另外还提供一些特殊目的的跨核通信方式以实现将一个数据广播给所有CPUs、运行类似map/reduce的操作等

## Futures and promises

- **并行范式 Paradigms for parallelization**
  协调多核工作的方式很多（Actor，CSP，immutable等等），有一些方式对程序员很友好，就像在单核CPU上开发软件一样，屏蔽了底层的并发细节，但是这就有可能会增加开销，不能充分利用多核的优势
- **软件开发的挑战 Software development challenges**
  - 进程是self-contained但是开销很高
  - 线程需要程序员精心设计，但同样很难debug
  - 纯粹的事件循环框架难以测试和扩展

  理想的解决方式应该有以下特性：
  - 易于设计和开发——对程序员友好
  - 充分利用现代硬件设备（多核CPU、超高速NIC、NVMe SSD等）
  - 易于debug
- **seastar的方案——`future`和`promise`设计**
  - `future`代表了一个还未知的结果，`promise`则代表这个结果的提供者，异步设计
  - `future/promise`不需要锁进行同步
  - `future/promise`不会额外分配内存
  - `future/promise`支持continuation的使用方式（即在一个`future`完成时使用结果自动执行另一个任务，使用`.then()`，在C++20开始支持协程的方式，因此不必再使用`.then()`的方式）

## High-performance networking

seastar在两个平台上支持一共四种网络模式：

- DPDK on Linux
- Standard socket on Linux
- Seastar native vhost on Linux
- Virtio on OSv

通常最优的方式是**在DPDK的环境下启用seastar自带的TCP/IP协议栈**

Linux内核协议栈的实现已经足够功能完备、成熟、高性能，但是对于真正的IO密集型程序，内核实现依然有一些局限性：

- **内核空间实现 Kernel space implementation**：由于网络协议栈实现在内核，导致大量网络操作可能会引起上下文切换，并且网络数据需要反复在内核态和用户态之间拷贝
- **分时 Time sharing**：Linux是分时操作系统，因此就依赖中断来通知内核需要处理IO任务，而中断并不高效
- **线程模型 Threaded model**：内核高度线程化，因此有大量的数据结构依赖锁的保护，虽然有大量的工作提升内核的多核扩展性，但是锁和竞争依然不可避免引入了额外开销，影响IO性能

seastar在用户态重新实现了TCP/IP协议栈，零拷贝zero-copy、无锁zero-lock、零上下文切换zero-context-switch，并且可以采用DKDP充分利用NIC的性能

## Message passing

seastar通过跨核使用无锁队列进行通信，采用了Actor模式，典型用法：

```cpp
// 从对端读取4个字节，在完成时放入temporary_buffer并调用lambda
return conn->read_exactly(4).then([this] (temporary_buffer<char> buf) {
  int id = buf_to_id(buf);
  // 将读取的字节转换成id后，提交给other_core进行处理，运行lambda
  return smp::submit_to(other_core, [id] {
    // other_core完成查询id后，返回结果给原core
    return lookup(id);
  });
}).then([this] (sstring result) {
  // 原core在收到id查询结果后调用此lambda，将结果发给对端
  return conn->write(result);
});
```

具体实现与分析见[Message Passing](Message_Passing.md)
