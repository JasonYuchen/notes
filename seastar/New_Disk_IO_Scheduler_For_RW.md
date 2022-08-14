# Implementing a New IO Scheduler Algorithm for Mixed Read/Write Workloads

[original post](https://www.scylladb.com/2022/08/03/implementing-a-new-io-scheduler-algorithm-for-mixed-read-write-workloads/)

## Introduction

在[前文 Disk IO Scheduler](https://github.com/JasonYuchen/notes/blob/master/seastar/Disk_IO_Scheduler.md)中提到过，先前的磁盘调度器模型对读写请求不加区分，视作同质的请求来建模并以此来控制磁盘的IO请求调度，这是不真实的，**实际的磁盘读写操作存在各自的特点**，在同质的假设下可能导致一个前端读请求被埋没在后端写请求中，前端读请求作为高优先级的任务反而延迟表现不佳

磁盘IO调度器的核心目标依然没有改变，即通过**协调读写请求以达到最佳并发性能和最大吞吐量**，既不overwhelm，也不underutilize，同时还必须考虑到Seastar的[shared-nothing](https://github.com/JasonYuchen/notes/blob/master/seastar/Shared_Nothing.md)架构，充分**尊重shards之间的公平性**

## Towards Modeling the Disk

磁盘的性能可以用四个参数来描述，即**读写的IOPS和读写的throughput**，Seastar对磁盘的建模经历了多个阶段，演化基本如下：

1. 通过配置选项确定磁盘的最大IOPS，随后调度器会避免超过该给定值，即仅考虑整体的IOPS
2. 磁盘的吞吐量与延迟互相影响，而仅仅IOPS无法反映出吞吐量的影响，因此引入了每个请求涉及读写的字节数，从而引入throughput，此时考虑整体的IOPS和throughput
3. 采用per-shard均分磁盘能力的模式会更容易导致磁盘underutilized，因此通过per-numa均分磁盘能力，多个shard共享一个fair-group的方式（本质上并没有改变对磁盘建立的读写模型）
4. 读写请求并不是同质的，显然磁盘对读和写的流程不同，因此更为细化的方式应是对磁盘进行四个参数的建模，即需要考虑读写的IOPS和读写的throughput（本文内容）

## The Data

通过一个简单的工具[Diskplorer](https://github.com/scylladb/diskplorer)来对磁盘施加变化的负载（例如纯读负载、纯写负载、读写混合等，**参考YCSB**），显然负载包含了上述四个参数的变化，最终体现的是延迟，因此结果应该是**5维的状态空间**，从而来衡量的真实性能，例如下图构建了R_IOPS-W_bandwidth的关系（平面中难以画出5维的结果，因此可以通过投影观察）：

![1](images/nioscheduler1.png)

图中（示例为NVMe磁盘）可以近似看出**半双工half-duplex**的表现，即当读IOPS加倍时，为了维持延迟不变则需要写吞吐量减半，即**近似线性负相关关系**

*HDD有较为特别的结果，在少于100MB/s也是近似线性负相关关系，但是在100MB/s上则完全无法服务读请求，类似**截断**的表现*

![2](images/nioscheduler2.webp)

## The Math

从上图可以推测可以**采用线性模型来描述磁盘**，即表达为：

```math
\frac{bandwidth_r}{B_r} + \frac{iops_r}{O_r} + \frac{bandwidth_w}{B_w} + \frac{iops_w}{O_w} \le K
```

其中$B_x$代表最大读写吞吐量，$O_x$代表最大读写IOPS，并且IOPS和吞吐量本身就是数据量对时间的一阶导数，因此上式又可以改写为对时间的一阶导数表达形式：

```math
\frac{d}{dt}(\frac{bytes_r + M_B \times bytes_w}{B_r} + \frac{ops_r + M_O \times ops_w}{O_r}) \le K
```

其中$M_B=\frac{B_r}{B_w}$为最大读写吞吐量之比，$M_O=\frac{O_r}{O_w}$为最大读写IOPS之比

为了便于衡量每个请求的磁盘影响力，即标准化请求代价，采用两个元组来描述一个请求，对读请求来说$T = \{1, bytes\}$，对写请求来说$T = \{M_O, M_B \times bytes\}$（即以读请求的IOPS和throughput为基准，写请求为相应"倍数"的读）从而一次操作的代价可以**基于读请求标准化**为：

```math
N(T)=\frac{T_0}{O_r}+\frac{T_1}{B_r}
```

从而将时间微分形式的关系转换为：

```math
\frac{d}{dt}(\sum_T N(T)) \le K
```

从而对于磁盘IO调度器来说，约束任意时刻的"速度"是非常困难的，但是**约束一个累积值的增长是简单的，即[令牌桶token bucket](https://en.wikipedia.org/wiki/Token_bucket)算法**

## The Code

令牌桶算法中，有两个输入值和一个输出值，输入值为实际请求的数据量和**被限流的令牌量**，从而要求数据量和令牌量必须匹配才能输出，通过**限制可控令牌的速率来间接控制不可控请求的速率**，如下图：

![3](images/nioscheduler3.png)

每个请求会在桶中等待直到能够获取到$N(T)$的令牌量才被真正发送给磁盘，而令牌则会以给定的速率$K$补充到桶中

需要**特别注意**的是，由于现代SSD自身存在一些垃圾回收等后台过程，因此依然可能会出现短暂的处理能力下降，而上述简单的令牌桶算法并不能感知到这一点，导致当**应用请求和SSD自身后台过程冲突时性能下降**

实际在Seastar中采用了二级令牌桶，即**令牌并不是凭空产生并补充给令牌桶的，而是在真正的IO请求完成时才会补充给二级令牌桶**，随后再以恒定速率$K$回充给一级令牌桶

![4](images/nioscheduler4.png)

采用上述修改后的令牌桶算法，确保了应用的所有IO请求实际上不会突破模型预测的理论上限和实际硬盘表现出来的上限，具体代码实现的流程分析[见此](#implementation)

## Results

结果可以从两个角度来衡量：

1. 磁盘调度器是否通过调度确保了IOPS和throughput始终在磁盘特性的**安全区域**内
2. 新算法下是否真实利用了磁盘对读写请求处理的异质性，从而**提高了读写请求的实际性能**

如下图来自于ScyllaDB 4.6和5.0的性能对比测试，测试配置细节见原文，前者采用前一版本的磁盘调度器，后者采用本文描述的新算法

1. **数据导入过程中的吞吐量对比**
   （左侧为4.6曲线代表整体吞吐量，右侧为5.0黄色代表写入绿色代表读取）

   - 4.6中维持了总带宽达到了800MB/s，而这就是磁盘的峰值性能，相比之下**5.0仅维持在710MB/s**这是因为考虑了读写的不同所产生的最后聚合带宽

    ![5](images/nioscheduler5.png)

2. **前端查询过程中的吞吐量对比**
   （左侧为4.6曲线代表整体吞吐量，右侧为5.0黄色代表写入绿色代表读取，三个阶段平台是因为读请求的入流速度分别为`10k, 5k, 7.5k`）

   - 5.0中对后台compaction过程的读写进行了更大的限制（我们通常总是希望后台进程能够在系统闲置时运作，在**前端请求忙碌时尽可能将资源用于降低前端延迟**，因此5.0更符合我们的目标），而相应的5.0前端请求所占用的磁盘资源更多
   - 从三个峰值的读请求占用带宽可以推测出4.6中的前端请求处理相对不稳定**甚至无法满足预期的读请求速率**，带宽存在波动，而5.0则非常稳定

    ![6](images/nioscheduler6.png)

Seastar考虑了两类延迟，**不可控的队列in-queue延迟（下图中的绿线）**和**可控的磁盘in-disk延迟（下图中的黄线）**，前者由应用层的实际请求数量决定并且由应用层来维护（例如Scylla引入[背压机制](https://www.scylladb.com/2018/12/04/worry-free-ingestion-flow-control/)），而后者则由Seastar来控制并追求最优

1. **数据导入过程中的延迟对比**
   （上图为4.6，下图为5.0，黄色代表磁盘in-disk延迟，绿色代表队列in-queue延迟）

   - 5.0的磁盘延迟约为443微秒，显著低于4.6版本的1.45毫秒，**延迟显著优化**

    ![7](images/nioscheduler7.webp)

    ![8](images/nioscheduler8.png)

2. **前端查询过程中的延迟对比**
   （上图为4.6，下图为5.0，黄色代表磁盘in-disk延迟，绿色代表队列in-queue延迟）

   - 后台compaction过程被抑制，从而充分多的资源用于处理前端请求（**short and latency sensitive reads**）
   - 5.0的前端请求磁盘处理延迟降低约一半，而**队列延迟则显著降低**，对于前端查询过程来说，队列延迟以及磁盘延迟一起作为上游可观测的延迟，而这个延迟得到了显著优化

    ![9](images/nioscheduler9.png)

    ![10](images/nioscheduler10.png)

## Implementation

[TODO: code analysis](https://github.com/scylladb/seastar/commit/837cadbe12c1b2be12158d48fffadfc8407c187d)
