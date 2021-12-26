# Keeping Latency Low and Throughput High with Application-level Priority Management

## Comparing throughput and latency

- **注重吞吐量的任务 Throughput computing** (~OLAP)
  - 最大化利用硬件
  - 充分采用缓冲技术，预读取等
  - 重在**优化任务的整体耗时**
- **注重延迟的任务 Latency computing** (~OLTP)
  - 保留一定的硬件资源**free cycles**为峰值流量做准备
  - 无法充分预测将要读取的数据，即难以预读取
  - 重在**优化每个独立任务的耗时**

## Isolate/identify different tasks

1. **每个操作都采用一个线程来完成**，从而高优先级的任务采用高优先级的线程，低优任务低优线程，依赖内核的调度
   - 易于理解，大量现有的框架支持
   - context switch代价昂贵，需要采用锁来保护数据也引入了阻塞代价
   - 无法控制内核对线程的调度，可能出现优先级反转等问题
2. **应用层任务隔离**，每个任务都在少量固定线程上运行，由应用层调度器来控制每个任务的执行顺序、并发程度等
   - 高度可控，粒度小，既是优点也是缺点
   - 低额外开销，协作式的调度
   - 对内核的依赖小，大量操作都发生在应用层，容易预测

## Schedule tasks

1. 当每次单一任务完成时就会考虑（不同任务队列中装载不同类型/优先级的任务）：
   - 当前任务队列是否耗尽
   - 时间片是否耗尽
   - 轮询I/O事件，例如`io_uring_enter`
   - 调度选择下一个任务，原则上调度目标就是**保证每个任务队列的`q_runtime / q_shares`相等**，不能采用Round-Robin类的简单调度
2. **抢占preemption时机的确定**
   - 每次都读取系统时间，根据时间确定是否让渡CPU使用权，这种方式可能**代价很高**
   - 计时器和信号量，计时结束时通过信号量来抢占，例如信号处理函数写入一个值，而执行中的任务检查这个值，但由于信号量在内核中的实现方式，并**不能很好的拓展**到多核上
   - **内核计时器**，例如linux AIO和io_uring的方式，非常高效

## Stall detector

基于信号量的方式来检测是否出现运行某个任务耗时过长（中间没有**及时检查preemption**，参考[`seastar::maybe_yield`](https://github.com/JasonYuchen/notes/blob/master/seastar/Coroutines.md#maybe_yield)）导致其他任务饥饿

## Dynamic Priority Adjustment

在运行时根据各模块资源的占用以及前端请求的压力动态调整每个模块的占比，即[动态调整优先级](https://github.com/JasonYuchen/notes/blob/master/seastar/Dynamic_Priority_Adjustment.md#backlog-controller)
