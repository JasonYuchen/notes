# 计时器 Timer

## 背景 Background

RPC或是其他程序框架，往往需要支持在xxx时间后做一个任务、在yyy时刻做一个任务，从而需要一个时间管理框架，需要注意的是：

- 在RPC中Timer往往用于RPC超时后的取消等逻辑，而超时并不常见，因此大部分Timer并不会真的触发，从而Timer本身的管理需要尽可能低开销
- 对于另一些必然会运行的定时任务，往往任务本身的耗时远超过Timer的管理开销，因此Timer的管理开销是高阶小量可以不必过于担心

下文主要讨论如何减小Timer本身的开销

## 做法 Approaches

1. 单线程框架中（以libevent, libev为代表的EventLoop框架，或是以GNU Pth, StateThreads为代表coroutine/fiber框架），通常采用小顶堆记录触发时间，采用`epoll_wait()`的方式等待最小/堆顶的超时时间，Timer的管理都在一个线程中，回调必须是较短非阻塞的否则会影响精度

2. 多线程框架中，通常采用独立的线程进行Timer管理，常见做法就是使用mutex保护小顶堆，基本原理与1类似，`epoll_wait()`的人为通知可以是**向监听的fd写入1个字节进行唤醒**，但缺点是全局抢占一个mutex，竞争激烈，线程较多的情况下表现往往不佳

   - **采用`condition_variable::wait()`的等待精度比`::epoll_wait()`更好**
   - 常见改进方法是进行**sharding，将Timer分布到多个小顶堆中**，运行时进行类似多个小顶堆归并排序的过程，但是缺点是mutex涉及的临界区直接包含了多个小顶堆的处理，临界区过大，且多个小顶堆导致缓存不友好

3. 时间轮的做法，**TimerThread以最小时间精度（例如1ms）定期唤醒**，这样插入删除Timer就不必再唤醒，只需要等TimerThread定期处理，**缺点是精度有限**，在现代操作系统的调度系统下频繁唤醒与睡眠的线程优先级最低，实际精度往往会大于2ms

4. brpc的做法：

   - 只使用一个TimerThread
   - 创建**Timer时散列到多个Bucket**并返回一个TimerID，每个Bucket维护一个Timer List和nearestTime，List并**不根据task run time排序**，nearestTime是整个Timer List内最近的run time
   - 删除Timer时根据TimerID直接定位到内存结构修改一个delete标志位，**Timer的删除在TimerThread中进行**
   - TimerThread唤醒运行时，会**获取所有Bucket的所有Timer并组成一个栈上的小顶堆**，（会首先设置TimerThread的nearestTime为无限大从而能够感知在运行时又有新的更近的Timer被创建），随后按小顶堆的顺序运行所有Timer，在获取Timer时如果检查到delete标志就不会加入小顶堆

    从而整个设计有如下优点:

   - Bucket内的List是创建的顺序，并不会根据run time排序，Timer的内存分配也是在临界区外完成的，因此**Bucket内的操作时O(1)且几乎不会锁冲突的**
   - 大部分插入的run time是递增的，早于Bucket内的nearestTime并需要和TimerThread的**全局nearestTime竞争的可能性很小**，即使竞争也仅是一个时间比较而不涉及数据结构变化，临界区极小
   - 极少数Timer会比当前nearestTime小并且唤醒TimerThread，但同样唤醒的过程也在全局的锁外
   - **删除是直接根据TimerID修改标志位**，并不参与全局的锁竞争
   - TimerThread自身维护**栈上的单个小顶堆，缓存更加友好**

## 评论 Comments

1. `epoll_wait()`的超时精度是ms级，`pthread_cond_timedwait()/condition_variable::wait()`的超时精度是us级（使用timespec精度是ns，但是实际会有数十us的延迟）
2. 使用wall-time而不是monotonic time，性能更好，但可能受到系统修改时间的影响
3. CPU支持nonstop_tsc和constant_tsc的情况下，brpc优先使用基于rdtsc的cpuwide_time_us，前两个标志代表rdtsc可以作为wall-time使用
