# System Design Primer

[repo](https://github.com/donnemartin/system-design-primer)

## 1. Scalability

[lecture](https://www.youtube.com/watch?v=-W9F__D3oY4)

- **Vertical Scaling**
  - CPU
  - Disk
  - RAM
- **Horizontal Scaling**
  - DNS
    若采用DNS对相同的地址返回不同后端服务器的地址（例如采用round-robin算法）来实现负载均衡，则问题在于客户端往往会缓存DNS的查询结果，导致部分后端服务器会面临更多的请求
  - load balancer
    在后端服务器前架设负载均衡器可以避免DNS的问题，由负载均衡器来决定每次请求提供服务的后端服务器，此时依然可以采用例如round-robin算法来实现负载均衡
  - Servers
    假如请求带有session信息，通常会被以文件的形式保存在后端服务器本地磁盘，此时如果连续的请求被分发到不同的后端服务器上，则session这一类共享的状态就会出现问题
  - RAID
    可以采用磁盘阵列、网络存储等共享存储的方式来存储session这一类共享的状态
- **Caching**
  - MySQL query cache
  - memcached
- **Replication**
  [more in ddia](https://github.com/JasonYuchen/notes/blob/master/ddia/05.Replication.md#主节点与从节点-leaders-and-followers)

  [more in dsaaa](https://github.com/JasonYuchen/notes/blob/master/dsaaa/16.Replicated_Data_Management.md#162-architecture-of-replicated-data-management)

  - **master-slave**
    主从模式，读写分离，主库写入，单向复制到从库，从库读取，主库存在单点问题
  - **master-master**
    双活模式，双向复制，每个主节点均可以再以主从的形式携带一个从节点
- **Partitioning/Sharding**
  [more in ddia](https://github.com/JasonYuchen/notes/blob/master/ddia/06.Partitioning.md#分区与副本-partitioning-and-replication)
- **High Availability**
  [an example](https://www.scylladb.com/2021/03/23/kiwi-com-nonstop-operations-with-scylla-even-through-the-ovhcloud-fire/)
  
  避免单点，冗余，多集群/多数据中心

## 2. Performance vs. Scalability

可扩展性scalability通常指的是系统的处理能力能够随着系统资源的增加而接近线性增加，这个处理能力通常指系统的吞吐量，而性能与可扩展性可以这样理解：

- 若有performance问题，则系统即使对单个用户的请求也会出现响应较慢
- 若有scalability问题，则系统对单个用户的请求响应非常快，但是重负载下响应很慢

`TODO: Scalability, availability, stability patterns`

## 3. Latency vs. Throughput

延迟指完成一次请求并响应需要的时间，而吞吐量指单位时间内能够完成的请求数量，可以简单类比为网络传输的延迟和带宽，通常系统的设计和优化目标在于**最大吞吐量和最低延迟**，可以参考[BBR的设计](https://github.com/JasonYuchen/notes/blob/master/linux/BBR.md)

## 4. Availability vs. Consistency

经典的CAP理论指出分布式系统只能牺牲一者选择另外两者：

- **一致性 Consistency**：指强一致性，大多数分布式系统若**牺牲一致性并不意味着完全不保证数据的一致性**，而是保证相对强一致性较弱的最终一致性、读写一致性、因果一致性等等
- **可用性 Availability**：指每个请求抵达分布式系统的**存活节点**都一定能够获得响应，而收到请求的存活节点不一定包含最新数据，大多数分布式系统若**牺牲可用性并不意味着完全不可用**，而是这些不包含最新数据的节点可能会拒绝请求并要求访问其他节点，客户端依然有可能从其他节点获得可靠的响应
- **分区容错性 Partition Tolerance**：指分布式系统在任意网络分区的情况下依然能够提供服务，实际上除了单机系统，**分布式系统一定会面临网络分区**，因此这往往不是一个选择而是一个必须项，从而大多数分布式系统的抉择是**CP或者AP**

### Consistency Patterns

> Networks aren't reliable, so you'll need to support partition tolerance. You'll need to make a software tradeoff between consistency and availability.

通常如果业务系统对数据的读写一致性要求高，不能容忍读取到过时的数据，应该选择CP；而如果对数据的读写一致性要求并不高，但是希望大多数时候都能提供服务，应该选择AP

- **弱一致性 Weak Consistency**
  通常系统只能*尽我所能*，不保证任何读写的一致性，可以在VoIP、视屏会议、多人游戏等系统中出现，例如游戏掉帧断线数据丢失等
- **最终一致性 Eventual Consistency**
  一次写入的数据最终一定能够被读取到（虽然严格来说没有限定时间上限，但是实践中通常根据不同的业务会有一个较好的保证，例如秒内可见），可以在DNS、邮件收发等系统中出现，DNS的条目更新是异步且最终生效的
- **强一致性 Strong Consistency**
  保证数据一旦写入就可以被立即读取到，即使横跨不同的节点，数据往往采用同步的方式复制到其他节点，可以在需要事务的系统例如数据库中出现

更加详细的各种一致性模型可以参考[此笔记](https://github.com/JasonYuchen/notes/tree/master/consistency#consistency-models)，来源于jepsen；在[*Distributed Systems: An Algorithmic Approach*](https://github.com/JasonYuchen/notes/blob/master/dsaaa/16.Replicated_Data_Management.md#163-data-centric-consistency-models)中也有讨论常见的一致性模型

### Availability Patterns
