# Seastar - Userspace Operating System

[seastar](http://seastar.io)

## Shared-nothing design

- **单核性能提升有限，多核成为主流**
  `TODO`
- **现代存储网络设备快速发展**
  随着存储和网络设备的急速发展，基于中断的设备已经受CPU的限制而无法达到设计IOPS
  
  例如一个2GHz主频的处理器处理在10GBps的网卡上1024bytes的包，每个包仅有1670个处理器时钟周期，而一次中断就会产生上下文切换等毫秒级的延迟

  绕过内核，解放CPU的[DPDK](https://www.dpdk.org)应运而生

  seastar在设计上就支持DPDK，并且重新实现了TCP协议栈以完全绕开Linux kernel
- **无共享：避免跨核同步**
  当需要跨核同步时，采用锁可能会导致竞争和等待，浪费CPU的时钟周期，即使采用原子变量，在竞争激烈时一次原子变量修改也会导致微秒级的耗时（普通变量仅需纳秒级耗时），参考[bvar的设计原理](https://github.com/apache/incubator-brpc/blob/8199994e54fb3077625a1539b21d63d8e9e75ca0/docs/en/bvar.md)
  ![arch1](images/arch1.png)

  **seastar在每个核心上运行一个应用程序线程，通过基于无锁队列显式的消息传递**（当跨核心通信不可避免时），而没有任何其他诸如共享数据、锁等同步方式，从而尽可能避免**锁unscalable lock primitives**、**缓存抖动cache bouncing**等影响
- **消息传递**
  简单的消息传递方式如下，显式的将一个任务递交给另一个cpu进行处理

    ```c++
    smp::submit_to(cpu, lambda);
    ```

  另外还提供一些特殊目的的跨核通信方式以实现将一个数据广播给所有CPUs、运行类似map/reduce的操作等

## Futures and promises

## High-performance networking

## Message passing
