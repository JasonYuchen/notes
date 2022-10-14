# [SIGMOD 2021] PolarDB Serverless: A Cloud Native Database for Disaggregated Data Centers

## 1 Introduction

云数据库的架构通常可以分为三大类：

- **单机一体式 monolithic machine**
  所有资源（计算、内存、存储等）高度耦合，一个物理机上应该分配的一体式数据库数量难以调整，整体资源利用率较低，且存在**伪共享false sharing**的问题，即一个实例的故障、运行异常会影响到同物理机上的其他实例，且数据库实例的容灾、恢复更为复杂
- **存储计算分离 separation of compute and storage**
  - 远端存储 virtual machine with remote disk
  - 共享存储 shared storage
  
  可以根据需求，动态扩容缩容计算资源和存储资源，整体资源利用率更高，但是由于CPU和RAM依然强耦合，不能更灵活的调整资源
- **分离式 disaggregation**
  进一步解耦计算和内存资源，使得处于通过高速网络连接的多个数据中心上的节点能够更加高效的利用资源，实现动态扩容缩容，不同的资源节点可以更高效的实现容灾恢复，避免资源耦合带来的伪共享问题

![01](images/polardb01.png)

## 2 Background

### 2.1 PolarDB

(TODO: Cloud-native database systems at alibaba: Opportunities and challenges)

PolarDB是一个采用共享存储架构的云原生数据库，从MySQL的基础上发展出来，底层存储层采用了PolarFS，包含一个**读写节点RW node**和多个**只读节点RO nodes**，每个节点都包含一个SQL处理器、事务引擎（例如InnoDB或是X-Engine）、缓存池来提供数据库服务

PolarFS是一个持久化、原子、可扩展的分布式存储服务，存储的数据会分片成10GB的数据块chunk，每个卷volume支持动态扩容至最多10000块数据，即100TB数据，每个数据块采用**Parallel Raft**（TODO: ）算法达成三副本共识，RW节点和RO节点通过redo logs和LSN来协调一致性，一个事务的流程如下：

![02](images/polardb02.png)

1. 事务准备提交
2. RW节点刷写所有redo log记录到PolarFS中
3. 事务此时可以提交
4. RW节点广播给所有RO节点此时的redo log已经更新，且最新的LSN位置
5. RO节点收到广播后，从PolarFS获取redo log的更新
6. RO节点将redo log的更新应用到自身的缓存页中
7. RO节点将自身应用的LSN位置回复给RW节点
8. RW节点后台会周期性的根据当前所有RO节点返回的LSN位置，将redo log中位置小于`min{LSN_RO_i}`的日志项清除，并且将比该位置更旧的脏页刷写进PolarFS
9. 期间，RO节点持续可以给携带有早于`LSN_RO_i`版本号（快照隔离）的读事务提供服务

- 当系统监测到某个RO节点的负载过重，导致`LSN_RO_i`更新较慢，与`LSN_RW`产生巨大滞后时，阻塞了上述RW节点依赖`min{LSN_RO_i}`刷写脏页的过程，就会被剔除出集群，避免影响整个系统的响应
- 前端的代理层提供了负载均衡服务，将写流量导入RW节点，将读流量分配到RO节点上

### 2.2 Disaggregated Data Centers

在分离式的数据中心，计算节点、内存节点、存储节点相互通过高速网络连接，并且采用了RDMA技术

![03](images/polardb03.png)

### 2.3 Serverless Databases

- auto-scaling
- auto-pause

## 3 Design

### 3.1 Disaggregated Memory

#### Remote Memory Access Interface

数据库通过librmem提供的接口来访问远端内存池，核心API主要有：

- `int page_register(PageID page_id, const Address local_addr, Address& remote_addr, Address& pl_addr, bool& exists);`
  管理页的生命周期，分配页（若不存在）并增加引用计数
- `int page_unregister(PageID page_id);`
  管理页的生命周期，减少引用计数并回收页（若引用计数归零）
- `int page_read(const Address local_addr, const Address remote_addr);`
  采用one-sided RDMA读取页
- `int page_write(const Address local_addr, const Address remote_addr);`
  采用one-sided RDMA写入页
- `int page_invalidate(PageID page_id);`
  **由RW节点使用，用于将所有RO节点上缓存的页全部失效化**

#### Remote Memory Management

一个数据库实例所占用的远端内存资源可能是由多个不同的内存结点所提供的，内存分配单元是1GB大小的slab，slab实际上由Page Array来实现：

- **Page Array, PA**：对应一个slab，由连续的16KiB大小的页构成，当一个内存结点启动时，所有PA所在的内存都会被注册给RDMA NIC，从而其中的page可以被远端节点通过one-sided RDMA操作

持有slabs的内存结点也称为slab node，当数据库实例启动时，其预先定义的slab需求（predefined buffer pool）就会从多个内存结点上预备slabs，其中第一个预备slab所在的slab node又称为home node，会包含额外的元信息：

- **Page Address Table, PAT**：包含有每个页的位置（slab node id & physical memory address）和引用计数信息
- **Page Invalidation Bitmap, PIB**：与PAT中的项对应，位图索引记录页是否失效，`0`代表页数据是最新的，`1`代表页数据已经被RW节点更新过并且尚未写回远端内存池；同时每个RO节点有个本地PIB用于记录该RO节点本地缓存的页是否失效
- **Page Reference Directory, PRD**：与PAT中的项对应，记录了每个页被引用的数据库实例，即对每个页调用`page_register`的数据库实例列表
- **Page Latch Table, PLT**：与PAT中的项对应，记录了页级别的锁page latch，用于保护页数据，同步多个数据库实例对同一个页的并发读写，主要用于保护B+树索引

**一次页的分配流程**如下：

1. 数据库实例调用`page_register`给home node，若page不存在则会由home node扫描所有slabs来找到最为空闲的slab并分配page
2. 若所有slab都被填满，则会寻找引用计数为0的页进行汰换（LRU策略），由于存储层支持page materialization offloading，因此即使是脏页也并不需要写回，可以直接汰换
3. home node根据找到的page，将地址更新进PAT，并返回page和PL给调用的数据库，显然**整个过程中home node仅依赖自身的元信息，不会与任何其他slab node交互**，从而提高性能
4. 若数据库实例的buffer pool**弹性扩容**，则会由DBaaS分配新的slabs给实例，并且更新上述元信息，而**弹性缩容**时就会根据LRU策略汰换掉一些页，**后台会进行迁移聚合**形成完整未被使用的slab再释放

#### Local Cache

远端内存即使采用的RDMA，其访问代价依然显著高于访问本地内存，因此数据库实例依然会持有本地缓存

当一个访问的页并不存在于本地和远端内存中时，数据库实例会采用libpfs从PolarFS中读取到本地缓存，根据需要（例如执行**全表扫描时，访问的页通常短时间内不会再访问，写入远端内存反而会降低资源利用率，污染缓存**）再采用librmem写入到远端内存中，内存节点和存储节点之间并不会有直接交互

当本地缓存满时，page同样采用LRU策略进行汰换，脏页被汰换时还需要写回远端内存，并且需要调用`page_unregister`进行引用计数的更新

本地缓存的命中率对系统整体性能有重大影响，因此引入了**预读取prefetching**等手段进行优化

#### Cache Coherency

PolarDB Serveless与[PolarDB的事务流程](#21-polardb)不同，后者的RO节点必须通过重放redo logs来重建pages而不能直接访问RW节点的缓存，在PolarDB Serveless中RW节点写回远端内存的页数据可以直接被RO节点访问，但是由于节点私有了本地缓存以及共享远端内存，**缓存一致性必须保证**

![04](images/polardb04.png)

PolarDB Serveless通过**缓存失效cache invalidation**策略来保证缓存一致性：

1. RW节点更新页数据
2. RW节点调用`page_invalidate`发起缓存失效
3. home node会在该页对应的PIB中设置页失效标记，即位图置`1`
4. 根据PRD找到所有引用该页的RO节点，这些RO节点的本地缓存中存有该页的缓存数据
5. home node向这些RO节点发起缓存失效
6. RO节点会在本地PIB中设置页失效标记，即位图置`1`
7. `page_invalidate`是一个同步操作，当所有RO节点返回时才成功，期间若某个RO节点超时未响应，则DBaaS会介入将该节点剔除
8. 回复RW节点缓存失效成功

每个事务会被分为多个微事务mini-transactions MTR，**每个MTR是一组连续的redo log records**，在一个MTR对应的redo logs都被刷写进PolarFS之前，所有该MTR中修改的页都必须采用`page_invalidate`进行标记，保证在远端内存中不存在一个状态是有效但数据是落后的页，故障恢复依赖该保证

### 3.2 B+Tree's Structural Consistency

并发控制包含两个部分，第一部分就是**物理上的并发安全**，避免多个线程同时修改索引导致出现空悬链接等不一致情况，第二部分就是**逻辑上的并发安全**，保证多个并发事务的不同隔离级别

在PolarDB Serverless中只有RW节点能够修改页，因此对于B+树不需要预防多个节点的冲突修改，关键在于RW节点对B+树进行结构修改时**Structure Modification Operations SMO**（例如B+树节点分裂）要确保RO节点不会看到不一致的节点，通过采用PLT上的**全局物理锁global physical latch**来确保这一点，同样有**共享锁S-PL**和**独占锁X-PL**两种模式，并发访问B+树的加锁流程基本与[单节点访问](https://github.com/JasonYuchen/notes/blob/master/cmu15.445/09.Index_Concurrency_Control.md#b%E6%A0%91-b-tree-latching)类似

当RW节点需要插入、删除数据时，流程如下：

1. 首先进行乐观操作，即只在本地进行加锁操作并尝试更新节点，假如不会引入B+树结构变化时，**即没有SMO时，就不需要PLT的协助**，此时只需要RW节点内的锁操作即可完成
2. 当发现需要SMO时，重新从根节点开始并且对相应的路径上都逐个采用X-PL加锁（当然也需要同样加上本地的独占锁X），直到插入、删除数据结束，节点分裂、合并结束时才会释放所有X锁和X-PL锁

当RO节点需要读取数据时，就会从根节点开始采用X-PL加锁，从而确保了RW节点和RO节点的安全

另外由于PLT由中心节点完成服务，因此另外两种优化措施引入以提升锁的性能：

- **stickness**，当SMO操作完成时，**PL并不会马上被释放，而是一直持有直到遇到RO节点的请求**为止，这样可以确保RW节点后续的操作可能继续持有锁而不需要频繁与PLT节点交互反复加解锁
- **RDMA CAS**

### 3.3 Snapshot Isolation

PolarDB Serverless**基于MVCC提供快照隔离Snapshot Isolation SI**，与InnoDB的实现一致，一条记录的前序版本由undo logs创建

事务基于快照时间戳来控制所能看到记录的版本，RW节点会维护一个中央时间戳/序列号Centralized Timestamp Sequence CTS来分配单调递增的时间戳给所有数据库节点：

- **读写事务需要从CTS获取两次时间戳**，事务开始时`ctx_read`和事务结束时`ctx_commit`，每条数据记录和undo log都有单独的列记录`ctx_commit`以及相应的`trx_id`，而读取操作只会返回携带的`ctx_commit`不大于该事务`ctx_read`的记录
- **只读事务只需要从CTX获取一次时间戳**，事务开始时`ctx_read`

但是一个较大的读写事务下，可能修改了非常多的数据记录，从而若在提交时同步写入所有行`ctx_commit`会带来较大的延迟（写入`ctx_read`则是随着事务进行发起的，因此代价平滑？），因此对于**大事务的`ctx_commit`写入是异步完成的**，这种情况下会导致并发的事务无法得知该已提交事务所影响的记录的版本，需要通过**查询RW节点上CTS的日志记录**来解决

CTS日志是一个环形数组，循环记录了最近的一批读写事务的`ctx_commit`，若该事务尚未提交则相应的`ctx_commit`为空，因此当任意数据库节点发现记录或undo log的`ctx_commit`为空时就可以查询CTS日志来确定该事务是否已经提交，是否可见

上述这种优化方式**类似于chat消息扩散写和扩散读的模型**，对于修改数据多的事务，写入每条数据的`ctx_commit`相当于"扩散写"，代价较大，此时选择其他并发事务主动来查询CTS日志，相当于"扩散读"，则不比等到所有`ctx_commit`写入完成，性能较高

由于获取时间戳的操作极为高频，PolarDB Serveless采用了one-sided RDMA CAS来原子获取并递增CTS计数器，并且CTS日志数组被放置在注册到RDMA NIC的连续内存中，从而RO节点能够极高效的查询CTS日志而不影响RW节点的CPU资源，避免RW节点称为瓶颈

### 3.4 Page Materialization Offloading

### 3.5 Auto-Scaling

## 4 Performance Optimization

## 5 Reliability and Failure Recovery

## 6 Evaluation

## 7 Related Work
