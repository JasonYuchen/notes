# [VLDB 2019] LSM-based Storage Techniques: A Survey

## 1 Introduction

Log-Structured Merge-tree, LSM-tree已经在绝大多数现代NoSQL系统中作为底层存储引擎所使用，例如BigTable、Dynamo、HBase、Cassandra、LevelDB、RocksDB等，并且在实时数据处理、图数据处理、流数据处理、OLTP等领域被广泛使用

这篇论文作为一篇综述，主要是总结了LSM-tree本身的特性、学术界和工业界基于LSM-tree的各种修改和提升，以及这些修改本身的权衡与利弊

## 2 LSM-tree Basics

### 2.1 History of LSM-trees

索引通常有两种更新策略，**原地更新 in-place updates**或者是**非原地更新 out-of-place updates**

- **in-place**：典型结构就是B+树，在更新时直接覆盖原先的数据，这种设计往往**对读请求更为友好**，读到的数据一定是最近更新的新数据，而**对写请求就不友好**，写数据时需要寻找到修改的数据点，这个过程引入了**随机I/O**，并且在更新和删除的过程中会导致碎片化，降低了空间利用率
- **out-of-place**：典型结构就是LSM树，所有更新都会被暂存在新的位置而不是直接覆盖旧的数据，从而写数据的过程是**顺序I/O**，**对写请求更为友好**，并且过程中并不会直接覆盖旧的数据因此也有利于简化故障恢复recovery的过程，但是**对读请求就不友好**，读到最新数据的过程更为冗长不像in-place可以直接读到最新数据，另一方面由于写入的数据可能存储在多个位置造成空间浪费，因此往往需要有后台清理服务持续**压紧数据compaction**

在LSM树之前的log-structured storage面临一个严重的问题：所有数据都追加到日志的末尾导致了查询性能低下，因为相关的记录互相分散存储在日志的不同位置不利于快速查询最新结果，同时这也导致了空间浪费

LSM树通过设计了一个**合并过程merge process**来解决上述问题，其特点与发展如下：

- 原始LSM树包含了一系列组成部分`C0, C1, ... Ck`，每一个部分都是B+树，`C0`存储中内存中并服务写请求，其余所有`C1, ... Ck`均存放在磁盘上
- 当任意`Ci`满时就会触发滚动合并过程，将`Ci`的一部分叶节点移动合并给`Ci+1`，也被称为**leveling merge policy**（由于实现的复杂性，这种合并设计并未被广泛使用）
- 在稳定的工作负载下，当level的数量固定时，**写性能在所有相邻的组成部分其大小比例相等时`Ti=|Ci+1|/|Ci|`达到最佳**（这影响了所有后续LSM树的设计与实现）
- 与原始LSM树同时期有另一种合并策略**stepped-merge policy**，其设计为一个LSM树由多个层构成，每一层`L`都由`T`个组成部分，当该层`L`充满时，响应的所有`T`个组成部分一起被合并为单个组成部分并作为`L+1`层的一个组成部分，也被称为**tiering merge policy**（被广泛使用在现在的LSM树实现中）

![3](images/LSM_survey3.png)

### 2.2 Today's LSM-trees

### 2.3 Cost Analysis
