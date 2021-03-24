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

seastar的**thread-per-core, TPC**设计，使得单个线程占用CPU在运行后不能出现任何会阻塞的操作，否则就会导致该线程上的所有工作都被阻塞，因此seastar的磁盘I/O极度依赖底层的XFS（XFS对Asynchronous IO的支持较其他文件系统而言更好），但**即使是XFS也会在一些情况下出现阻塞**

XFS为了提升并行度，会从一个allocation group中为事务日志和元数据更新分配缓存，而如果一个allocation group在被耗尽时、或出现竞争时就可能导致I/O submission回退到同步行为，即阻塞

### 阻塞层 The Linux block layer

当某个磁盘上有超过128个（通过`sudo cat /sys/class/block/sda/queue/nr_requests`可以查看，并且可以配置）尚未返回的I/O请求时，Linux内核阻塞层就会直接拒绝接收新的请求，并**同步等待**直到有请求返回才会接收新的请求

但是**简单增加该值并不能解决阻塞问题**，一方面因为出现阻塞往往是大量请求难以被及时处理或是有其他故障，增加该值只会掩盖表面问题，另一方面假定每个请求延迟200微妙，大小为128kB，则限制请求数为128时可以达到600MB/s的吞吐量——这已经是主流SATA SSD的设计吞吐量了（NVMe SSD的吞吐量可以达到2000MB/s以上）

### 磁盘内队列 The disk array itself

现代磁盘内部也会有队列来提升并发I/O性能，当请求足够多时队列就会被填充满，此时请求的处理延迟就会不断增加而吞吐量却不会再改变——**系统过载overload**，可以参考[Little's Law](https://github.com/JasonYuchen/notes/blob/master/brpc/flow_control.md#littles-law)

![ioscheduler2](images/ioscheduler2.png)

显然如同Little's Law中的分析，当磁盘过载后，继续发起新的请求只会增加延迟，没有任何益处，且有可能导致上游服务超时不断重试引起雪崩

### 限制磁盘并行度 Limiting the disk parallelism in practice

scylla/seastar通过在运行真正的服务前，首先在系统上运行`iotune`工具来发现系统的最佳承载能力——`--max-io-requests`

`TODO: 增加对比测试结果 https://www.scylladb.com/2016/04/14/io-scheduler-1/`

## 调度器的设计 Seastar Disk I/O Scheduler Design - The I/O Queues

在所有的调度算法中，都需要考量**公平性fairness**的问题，往往是基于预先定义的优先级进行任务的调度，seastar中称之为`priority classes`

从上文中可以看出，Linux内核层对每一块磁盘的未返回请求数量有一个全局的限制，而seastar采用了shared-nothing的设计，因此在seastar中的**每个shard都需要有一种机制与其他shards一起分配磁盘I/O的全局限制量`nr_requests`值**

最简单的设计就是均分全局数量，seastar的内存设计（每个shard均分可用内存）就是如此，在工作量能够较好的均匀分配（没有**倾斜skew**）到所有shards上时资源利用较为充分，否则也会出现**碎片问题fragmentation**

尤其是当全局限制较小甚至少于shards数量时，每个shard的请求配额极小，极有可能造成部分shards忙碌但更多shards空闲进而浪费磁盘I/O资源，因此对于磁盘I/O来说，seastar并没有简单采用基于shards均匀分配的方案，而是采用自适应的方式，**基于多个`I/O Queue`进行均分**，每个`I/O Queue`均分全局限制量，而多个shards共享一个`I/O Queue`（如果全局限制足够大，则每个shard独享一个`I/O Queue`，此时就回退到了简单均分的方案）

![ioscheduler3](images/ioscheduler3.png)

从而seastar的磁盘I/O调度器就需要在考虑`priority classes`时对每个`I/O Queue`进行shards任务的分配和调度

**注意：当前设计已经不再是最新的，TODO: 分析scheduler 2.0的设计，[`--num-io-queues`选项已经被废弃](https://github.com/scylladb/seastar/issues/864)**

### 选择队列数量 Choosing the right number of I/O Queues

每个`IO Queue`都会被固定放置在一个shard中，此shard就被称为该`IO Queue`的协调者coordinator，并且**所有与此`IO Queue`交互的shards都必须属于同一个[NUMA节点](https://en.wikipedia.org/wiki/Non-uniform_memory_access)**（这个限制也隐式规定了整个seastar下的`IO Queue`数量最少为NUMA节点的数量）

可以通过`--num-io-queues`来显式指定数量，显然越少则局部的并行性越好磁盘利用越充分，但跨核心通信引入的I/O代价也越高（通常一个较大的I/O请求本身例如128KB就足够掩盖跨核通信带来的开销），因此在部署系统时需要根据硬件性能和工作负载类型进行设计（seastar提供了`iotune`工具进行自适应调整）

### 优先级 Priority classes in Scylla

### 队列的内部功能 Internal Functioning of an I/O Queue

### Scylla的调度器 Scylla's I/O Scheduler in Practice

### 整合所有设计 Putting it all together

## 总结 Summary and future work
