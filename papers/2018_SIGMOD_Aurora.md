# [SIGMOD 2018] Amazon Aurora: On Avoiding Distributed Consensus for I/Os, Commits, and Membership Changes

[相关笔记mit6.824: Aurora](https://github.com/JasonYuchen/notes/blob/master/mit6.824/10.Aurora.md)

## 简介 Introduction

Aurora在设计上考虑的**最大故障域就是整个AZ (Availability Zone)宕机**，一个AZ是Region的子集，与其他AZ通过高速网络连接，并且相互隔绝了绝大多数故障源（电源、网络、软件部署、自然灾害等）

Aurora支持**AZ+1**的故障策略，通过六份数据拷贝分布在三个AZ中，write quorum为4/6，read quorum为3/6，采用六份数据就可以在单个AZ整体宕机以及另一个AZ中存在一个节点宕机的情况下继续提供读服务，如下图倒数两行分别是AZ+1故障可以提供读和AZ故障可以提供写的情况：

![Aurora1](images/Aurora1.png)

Quorum系统在高性能的关系型数据库中往往不被使用，虽然quorum可以带来高可用性，但是分布式系统中所使用的quorum算法例如2PC、Paxos commit、Paxos membership change等往往会带来极高的额外代价，**基于这些算法的系统通常会有优良的可用性和可扩展性，但是性能不佳**，往往比单节点的关系型数据库延迟要高数量级以上

Aurora通过**quorum I/O、本地状态locally observable state、单调递增的日志monotonically increasing log ordering**来提供高性能的非阻塞容错I/O、提交和成员变更操作：

- 采用异步的任务流来完成write，建立本地一致性点local consistency points，并采用一致性点来完成commit以及crash recovery
- 避免quorum read以及将read操作扩展到所有副本
- 采用quorum sets和epochs来实现非阻塞的可逆成员变更，从而进行故障容错、存储扩容等

## 高效写入 Making Writes Efficient

### Aurora系统架构 Aurora System Architecture

Aurora采用了存储和计算分离的架构，数据库实例与存储服务相分离，每个数据库实例作为一个SQL endpoint，包含了绝大多数传统数据的组件，例如查询处理、事务、锁、缓存、undo日志等，而**redo日志、数据块与垃圾回收、备份与恢复**则被分离由存储服务提供

存储服务节点实际完成的工作如下：

1. （前端）接受redo记录
2. （前端）将redo记录写入一个update队列并响应ACK给相应的数据库实例
3. （后端）对redo记录进行排序和分组
4. （后端）通过gossip协议与其他存储服务节点通信，从而补全redo记录的缺失部分
5. （后端）将完整的redo记录合并处理成数据块
6. （后端）将数据块存储到AWS S3中
7. （后端）不会再被使用到的过旧数据块会被垃圾回收
8. （后端）周期性的校验数据来确保校验和与磁盘上的数据一致

![Aurora2](images/Aurora2.png)

Aurora的最小错误单元就是一个段文件segment，一个segment代表了数据库卷中不超过10GB的可寻址数据块，**每个segment都会复制到6个节点上（称为protection group）**，采用`V = 6, V_w = 4, V_r = 3`的quorum规则来访问这些副本，并且有**10秒的窗口期来检测和恢复segment的错误**，因此当出现10秒内有一个AZ+2个节点的4份数据都出错时才会导致整个系统不可用，否则的话就会及时进行成员变更和容错来保持quorum可用

每个protection group对应一个segment，从而多个protection groups（每个segment对应的protection group的6个节点未必是相同的一批存储节点）能够构成完整的一份数据库卷，并对应一个数据库实例，**数据库实例的redo log就通过这种segment的方式分割并被备份在多个存储节点上的protection groups**，而数据库卷通过一个单调递增的日志顺序号**Log Sequence Number, LSN来访问redo log**，这也是Aurora避免分布式共识的核心

### 写入 Writes in Aurora

在Aurora的设计中，数据库实例与存储服务之间唯一通信的内容就是redo log，而**存储服务节点上通过不断的应用redo log进行数据库数据页的构建以及按需提供读服务**

当数据库实例中对数据页缓存进行了修改时，就会发送响应的redo记录到日志缓存，数据库实例会周期性的将日志缓存shuffle为将要发送给针对不同存储服务节点的write buffer，并异步的将这些write buffer发送给存储服务节点以及**根据所有存储服务节点返回ACK的情况来构建一致性点consistency point**

每个redo log记录会记录：

- **volume**中前一条redo log的LSN
- **segment**中前一条redo log的LSN：这条segment LSN chain用于**判断是否存在日志空洞log hole**，从而可以基于gossip协议进行日志补全
- **block**中前一条redo log的LSN：这条block LSN chain用于**存储服务节点可以针对某一块数据块单独进行构建materialize**以满足读需求

Aurora中所有日志的写入操作（包括commit日志）都是**完全异步化**的：异步发送给存储节点、由存储节点异步处理、数据库实例异步接收存储服务节点的ACK的

### 存储的一致性和提交 Storage Consistency Points and Commits

### 故障恢复 Crash Recovery in Aurora

## 高效读取 Making Reads Efficient

### 避免quorum读取 Avoiding quorum reads

### 读取的扩容 Scaling Reads Using Read Replicas

### 结构化一致性 Structural Consistency in Aurora Replicas

### 快照隔离 Snapshot Isolation and Read View Anchors in Aurora Replicas

## 宕机和成员 Failures and Quorum Membership

### 采用quorum集合来改变成员 Using Quorum Sets to Change Membership

### 采用quorum集合来减少成本 Using Quorum Sets to Reduce Costs
