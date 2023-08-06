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
    若采用DNS对相同的地址返回不同后端服务器的地址（例如采用round-robin算法）来实现负载均衡，则问题在于**客户端往往会缓存DNS的查询结果**，导致部分后端服务器会面临更多的请求
  - load balancer
    在**后端服务器前架设负载均衡器**可以避免DNS的问题，由负载均衡器来决定每次请求提供服务的后端服务器，此时依然可以采用例如round-robin算法来实现负载均衡
  - Servers
    假如请求带有session信息，通常会被以文件的形式保存在后端服务器本地磁盘，此时如果连续的请求被分发到不同的后端服务器上，则**session这一类共享的状态**就会出现问题
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

通常网络分区在分布式系统中是必然会出现的，因此**必须满足P**，从而分布式系统往往在A和P中做权衡：

- **CP系统**：从被网络分区后的远端节点等待结果会超时，此时服务不可用但是数据始终是一致的；当业务要求原子读写/强一致性时选择这种模型
- **AP系统**：任意节点都可以返回最新的结果，尽管被网络分区后这些结果并不一定一致，当网络分区被解决后需要一些时间来保证所有数据一致；当业务能够容忍一些不一致，并且更希望快速获得结果和更高的可用性时选择这种模型

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

- **故障转移 Failover**
  - Active-passive
    主备容灾，基本模式就是主服务器通过发送心跳给备份服务器保活，当心跳超时后备份服务器就会成为主服务器继续提供服务
  - Active-active
    双活容灾，基本模式就是根据负载均衡服务器将请求均分到所有服务器上，每个服务器都承担一部分流量
- **副本备份 Replication**
  通常指数据库数据的备份容灾模式
  - Master-slave
    主库负责数据的读写请求，并将数据复制到从库（取决于同步/异步复制模式，可能存在一部分数据差异），而从库只负责数据的读请求（由于数据差异，存在读到非最新数据的可能）
  - Master-master
    多个节点互为备份，每个节点都可以接收读写请求，由数据库内部的协议来确保读写的一致性

[transactions acorss datacenters](https://snarfed.org/transactions_across_datacenters_io.html)

- **定期备份**，弱一致性，通常没有事务支持
- **主从备份**，通常异步备份从而延迟较低，弱一致性/最终一致性，例如MySQL binlog
- **双活备份**，处理冲突写入**需要串行化协议serialization protocol**，通常也是异步备份，弱一致性/最终一致性
- **两阶段提交2PC**，分布式事务，原子提交协议，同步协议延迟高，吞吐量较低，[详细见此note](https://github.com/JasonYuchen/notes/blob/master/cmu15.445/23.Distributed_OLTP.md#%E4%B8%A4%E9%98%B6%E6%AE%B5%E6%8F%90%E4%BA%A4-two-phase-commit-2pc)
- **PAXOS**，分布式共识，多数派读写，容忍少数派宕机，相较2PC轻量但延迟依然高，[详细见此note](https://github.com/JasonYuchen/notes/blob/master/papers/2001_Paxos_Made_Simple.md#2001-paxos-made-simple)

|            |Backups|M/S     |MM      |2PC   |PAXOS |
|:-:         |:-:    |:-:     |:-:     |:-:   |:-:   |
|Consistency |Weak   |Eventual|Eventual|Strong|Strong|
|Transactions|No     |Full    |Local   |Full  |Full  |
|Latency     |Low    |Low     |Low     |High  |High  |
|Throughput  |High   |High    |High    |Low   |Medium|
|Data loss   |Lots   |Some    |Some    |None  |None  |
|Failover    |Down   |R-only  |R/W     |R/W   |R/W   |

## 5. Domain name system, DNS

`TODO`

## 6. Content delivery network, CDN

内容分发网络通过一组分布的代理服务器，就近向用户提供内容（由站点的DNS服务器告知客户端需要连接的CND节点），避免所有内容都由主服务器提供，从而显著提升系统整体性能，CDN分成两种主要的模式（或是对不同内容采用混合模式）：

- **Push CDNs**
  由**主服务器完全负责所有CDN上的数据修改、生效和有效期**，因此通常主服务器需要将所有数据都完全存储到所有CDN节点（类似饿汉模式），从而**占用较多的存储空间**，但相应的客户端访问完全由CDN提供服务，**减少了主服务器的流量和压力**；对于流量较少且修改不频繁的内容而言，采用Push的效果较好
- **Pull CDNs**
  每当CDN收到用户请求且无**法找到所需的数据时才会去主服务器请求相应的内容**（类似懒汉模式），此时主服务器会设置**数据的有效期time-to-live, TTL**，这种模式下不需要存储所有数据到CDN上，**占用较少的存储空间**，但可能会有较频繁或周期性的CDN数据拉取请求（即使数据并未改变但过期也会触发冗余的拉取）导致**主服务器流量和压力较大**；对于流量较大（大多访问到近期的新数据）且修改频繁的内容而言，采用Pull的效果较好

采用CDN也有一些显而易见的缺点：

- 成本显著受流量的影响（不采用CDN可能会有其他成本）
- 由于TTL的存在，定期更新会导致客户端可能读取到过期数据**stale read**
- 对于存放在CDNs上的数据，主服务器需要将相应的地址指向CDNs

## 7. Load balancer
