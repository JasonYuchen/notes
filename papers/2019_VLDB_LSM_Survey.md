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
- 与原始LSM树同时期有另一种合并策略**stepped-merge policy**，其设计为一个LSM树由多个层构成，每一层`L`都由`T`个组成部分，当该层`L`充满时，相应的所有`T`个组成部分一起被合并为单个组成部分并作为`L+1`层的一个组成部分，也被称为**tiering merge policy**（被广泛使用在现在的LSM树实现中）

![3](images/LSM_survey3.png)

### 2.2 Today's LSM-trees

1. **Basic Structure**
   现在的LSM树设计基本上沿袭了原始LSM树的核心设计，并且所有磁盘上的**组成部分都是不可变的immutability**，当需要合并时直接读取被合并的组成部分并生成新的组成部分，从而并发控制和故障恢复更为简单

   一个LSM树的组成部分可以采用任何索引结构，现在的LSM树通常会采用利于并发的数据结构例如**跳表skip-list或是B+树作为内存中的组成部分**，而采用**B+树或有sorted-string table, SSTables作为磁盘上的组成部分**，一个SSTable包含一系列数据块以及一个索引块，数据块存储根据key排序后的key-value对，而索引块存储所有数据块的key范围，[具体见此](https://github.com/JasonYuchen/notes/blob/master/ddia/03.Storage_and_Retrieval.md#2-sorted-string-table-sstable%E5%92%8Clog-structured-merge-trees-lsm-trees)

   一个点查询请求需要依次搜索多个LSM树的组成部分来**确定数据的最新值即reconciliation**，通常从内存的组成部分开始逐个遍历，**一旦找到key就是最新的值**，但是没有找到就需要一直遍历直到所有组成部分才能最终确定这个key是否存在以及对应的value的最新值，对于**范围查询则是同时遍历所有组成部分**并且将满足范围的结果加入到结果集中

   随着正常运行，磁盘上的组成部分会越来越多，此时需要通过前文描述的合并过程进行合并组成部分、仅保留key的较新值、剔除被标记删除的值，如上图所示，最优的情况就是每一层之间的尺寸比例为`T`，因此在**leveling merge policy下每一层只有单个组成部分**，并且这个组成部分的尺寸是上一层的`T`倍大小，当尺寸达到上一层的`T`倍时该层才会被放入下一层；而在**tiering merge policy下每一层都有至多`T`个组成部分**，当任意层达到`T`时就会全部被合并为一个组成部分并放在下一层中

   对于leveling merge policy而言，由于每一层只有一个组成部分，因此**对读请求相对友好，每一层只需查询一个组成部分**，例如LevelDB和RocksDB；对于tiering merge policy而言，由于每一层可以有更多的组成部分，因此**对写请求更加友好，多个组成部分减少了合并的频率**，例如Cassandra

2. **Well-Known Optimizations**
   在现在大多数LSM树实现中都采用了以下两大优化措施：
   - **布隆过滤器 Bloom Filter**
     当插入新key时，key被散列多次映射到位向量的多个不同位置，这些位置被置1；当判断一个key是否存在时就通过相同的方式判断位向量的多个不同位置是否为1，只要有1个位置是0就说明不存在，如果所有位置都是1就说明**可能存在**，需要考虑到散列冲突的情况，因此布隆过滤器是一个概率查询结构，可能存在假阳性，但一定不存在假阴性

     **对磁盘上的LSM树组成部分构建内存中的布隆过滤器**，从而当一定不存在时就可以避免读取磁盘数据，显著提升查询速度，当可能存在时才读取磁盘上的B+树索引进而精确判定组成部分中是否有这个key

     另一种措施是**只对组成部分的B+树索引叶结点构建布隆过滤器**，此时依然需要首先通过B+树索引非叶节点部分（通常这种方式往往认为非叶节点部分足够小完全可以放置在内存中）来确定叶节点，随后先读取布隆过滤器来确定叶节点中是否有可能存在需要的数据而不是真正去读取叶节点数据

     布隆过滤器的假阳性概率为 $(1-e^{-kn/m})^k$，其中 $k$ 为散列函数的数量， $n$ 为key的数量， $m$ 为位向量的长度，假阳性率最低时 $k=(m/n)ln2$ ，**实践中通常直接采用`10 bits/key`从而获得约1%的假阳性率**
   - **分区 Partitioning**
     单个磁盘组成部分较大时会有诸多性能劣化的可能性，采用**范围分区**的方式将组成部分分割为多个较小的分区，为了便于理解**每个分区称为一个SSTable**，将一个较大的组成部分分为多个小的SSTable后，**有利于合并操作时粒度更细**，即单次处理较小的数据量、产生较小的中间结果、处理时间较短，另一方面也**有利于分割处理数据的范围减少重叠**，只需要合并key范围存在重叠的SSTable

     **分区的优化手段可以与合并策略组合使用**，例如leveling + partitioning或tiering + partitioning如下图，实际实现中例如LevelDB和RocksDB完全实现了leveling + partitioning

     图4中可以看出level 0的组成部分并没有分区，这些内存组成部分是直接刷写到磁盘上的，当需要将SSTable从`L`层合并到`L+1`层时所有`L+1`层中与该SSTable存在key范围重叠的SSTables一起参与合并，如图中的`0-15`和`16-32`需要与`0-30`合并，并且合并后原先的`0-30`就会被垃圾回收，**由于触发合并时任意一个`L`层的SSTable都可以被选择，因此可以有不同的选择算法**，LevelDB采用简单的round-robin策略来减小总的写入成本

     ![4](images/LSM_survey4.png)

     tiering merge policy同样可以使用分区，但是问题在于**tiering下一层可以有多个组成部分并且其key范围存在重叠**，从而当分区后可能导致多个存在重叠范围的SSTables，而leveling merge polcy中每一层只有一个组成部分可以简单分区成互不重叠的SSTables，此时如上图中的设计可以引入**垂直分组vertical grouping**或**水平分组horizontal grouping**对SSTables进行管理以确保正确的合并

     **分区情况下的合并可能产生多个新的SSTables**，这是因为**合并后需要适配下一层已有组的key范围**，具体过程如下：
     - **垂直分组**中每个组内的SSTables都存在重叠，而组之间则不存在重叠，从而**触发合并时以组为单位**，合并某一组内的所有SSTables来产生下一层新的SSTable并插入对应的组中，例如上图中垂直分组情况下`0-31`和`0-30`合并后（根据下一层的分组情况`0-13`和`17-31`）实际产生了`0-12`和`17-31`两个子SSTables，分别加入下一层key范围不重叠的组
     - **水平分组**内每个SSTables之间都不存在重叠，因此一个组成部分进行逻辑分区后就可以直接作为一个组，每一层的多个组成部分作为多个组，只有一个作为**活跃组**并接收上一层合并产生的新SSTables，当合并时需要选择所有组的key重叠部分进行合并，产生的新SSTables就加入下一层的活跃组，例如上图中水平分组情况下`35-70`和`35-65`合并后（适配下一层已有分组中的`32-50`和`52-75`）实际产生了`35-52`和`53-70`两个子SSTables，但水平分组的情况下两者一起加入下一层活跃组

3. **Concurrency Control and Recovery**
   LSM树的并发支持通常采用**锁机制locking scheme**或是**多版本机制multi-version scheme**来实现，由于LSM树本身会保存key的多个版本并且其合并操作会丢弃过时的数据，因此很自然的可以支持多版本并发控制，但是LSM树特有的**合并操作会对元数据做出修改**，因此必须被同步，通常可以对**每个组成部分维护一个引用计数**，在访问LSM树前，首先获得**当前所有活跃组成部分的快照**，并增加其引用计数从而保证使用中的组成部分不会因为合并而被垃圾回收

   由于所有写入首先都追加到内存中，使用WAL就可以保证写入数据的持久可靠，**通常的LSM树会采用[no-steal](https://github.com/JasonYuchen/notes/blob/master/cmu15.445/20.Logging.md#%E7%BC%93%E5%AD%98%E6%B1%A0%E7%AD%96%E7%95%A5-buffer-pool-policies)的缓存管理策略**，内存中的组成部分只有在所有活跃的写入事务结束时才会被刷写到磁盘上，在**恢复时因为no-steal的策略从而只需要redo所有成功的事务即可，不需要undo未完成的事务**，因为这些事务并没有刷写到磁盘上；另外需要确保活跃组成部分的列表也能够被恢复，在LevelDB和RocksDB中这通过**额外维护一个元数据日志metadata log来记录所有结构上的修改**，例如SSTables的增减

### 2.3 Cost Analysis

采用一次操作会引入的磁盘I/O数量作为代价分析非分区形式LSM树的基本操作如写入、点查询、范围查询以及空间放大，给定如下参数：

- LSM树拥有层数 $L$
- 层间size比例为 $T$，并且插入操作和删除操作的数据量相同即层数 $L$ 稳定
- 数据页能存放的entries数量为 $B$
- 内存中组成部分的数据页数量为 $P$，从而内存中的组成部分至多存放记录数量为 $B \times P$
- 任意 $i$ 层的组成部分最多存放记录数量 $T^{i+1} \cdot B \cdot P$
- 假定总共有 $N$ 条记录则最大的层持有记录约 $N \cdot \frac{T}{T+1})$，并且总共的层数约为 $L = \lceil \log_T(\frac{N}{B \cdot P} \cdot \frac{T}{T+1}) \rceil$

不同操作的（最坏情况）代价分析如下：

- **写入代价**
  也称为**写入放大write amplification**，指平均每次插入一条记录最终需要的I/O次数（这条记录被**最终合并到最大的level时的I/O次数**），对leveling策略来说，每一层需要合并 $T-1$ 次直到该层达到最大体积才能被移动至下一层，从而其最终代价为 $O(T \cdot \frac{L}{B})$ （注意$B$是每一个数据页能存放的记录数量，而磁盘一次I/O操作一个数据页，从而平均每次I/O操作$B$条记录）；对tiering策略来说，每一层可以有多个组成部分并且仅需一次合并就被移动至下一层，从而其最终代价为 $O(\frac{L}{B})$
- **点读取代价**
  在没有bloom filter的情况下，一次点查询就需要遍历所有组成部分，因此其代价对leveling策略为 $O(L)$，而对tiering策略为 $O(T \cdot L)$

  在有bloom filter的情况下，读取的性能被大大改善，因为当key不存在时只有bloom filter假阳性才会触发磁盘I/O，假定bloom filter有M位向量，且所有组成部分的假阳性率相等均为 $O(e^{- \frac{M}{N}})$ ，则对于不存在key的查询代价对leveling策略为 $O(L \cdot e^{- \frac{M}{N}})$，对tiering策略为 $O(T \cdot L \cdot e^{- \frac{M}{N}})$，另外对于存在的key的查询至少需要一次I/O读取数据，由于假阳性率远小于1，因此综合来看对key存在的查询两种策略下查询代价均为 $O(1)$
- **范围读取代价**
  范围查询的I/O代价取决于查询本身的**选择性selectivity**，假定一次范围查询最终会获得 $s$ 条记录，则称 $\frac{s}{B} > 2$ 的查询为**长查询long query**，反之为**短查询short query**，显然长查询会涉及更多的记录，往往需要查询到最大的层，则其查询代价就由最大的层确定，而短查询通常只需要涉及到单个磁盘数据页，则对每个组成部分都发起一次磁盘I/O，从而长查询在leveling策略下的代价为 $O(\frac{s}{B})$ 而在tiering策略下的代价为 $O(T \cdot \frac{s}{B})$；短查询在leveling策略下的代价为 $O(L)$ 而在tiering策略下的代价为 $O(T \cdot L)$
- **空间放大**
  空间放大主要是由于多次合并中同一个key可能保存有不同时期的数据，从而占用了额外的空间，假定空间放大定义为总记录数除以unique记录数
  
  对leveling策略来说，最坏情况就是前 $L-1$ 层（包含接近总体数据量的 $\frac{1}{T}$）均是对第 $L$ 层的数据更新，即第 $L$ 层完全是过时数据，此时空间放大率为 $O(\frac{T+1}{T})$；对tiering策略来说，最坏情况是最大的第 $L$ 层所有 $T$ 个组成部分均包含完全相同的keys，此时空间放大率就是 $O(T)$

|Operation|Leveling|Tiering|
|:-:|:-:|:-:|
|write|$O(T \cdot \frac{L}{B})$|$O(\frac{L}{B})$|
|point query|$O(L \cdot e^{- \frac{M}{N}})$ or $O(1)$ ($O(L)$ without bloom)|$O(T \cdot L \cdot e^{- \frac{M}{N}})$ or $O(1)$ ($O(T \cdot L)$ without bloom)|
|range query/long|$O(\frac{s}{B})$|$O(T \cdot \frac{s}{B})$|
|range query/short|$O(L)$|$O(T \cdot L)$|
|space|$O(\frac{T+1}{T})$|$O(T)$|

**体积比 $T$ 会对LSM树的性能产生显著影响**，并且对leveling和tiering的策略影响不同：

- leveling策略下每层只有一个组成部分
  - 查询性能更高
  - 空间利用率更高
  - 会导致频繁的合并操作，写入代价需要乘上 $T$
- tiering策略下每一层可以有 $T$ 个组成部分
  - 显著减少了合并操作的频率
  - 提升了写入的性能（**合并操作会导致写入暂停write stall**）
  - 牺牲了读取的性能，读取代价需要乘上 $T$
  - 空间利用率低，写入放大严重，空间代价额外乘上 $T$

可见有诸多方式来设计并调整LSM树的性能，这也服从**RUM猜想**，即数据结构的访问方式需要**在Read cost，Update cost，Memory/storage cost中权衡**

## 3 LSM-tree Improvements

### 3.1 A Taxonomy of LSM-tree Improvements

LSM树有诸多缺点，也是诸多研究希望改善的方面：

- **写放大 Write Amplification**：写放大导致了一条记录可能被反复多次写入实际设备，过度使用现代SSD存储设备，同时也限制了写入性能
- **合并操作 Merge Operations**：合并会导致缓存失效以及写暂停write stall
- **硬件 Hardware**：原始LSM树是针对HDD设计的，使用顺序写入代替随机写入，因为HDD顺序I/O性能远高于随机I/O性能，而现代更为广泛使用的SSD/NVM则拥有不同的I/O表现，通过针对不同的底层存储设备的特性可以修改LSM树的实现来充分挖掘硬件的性能
- **特殊负载 Special Workloads**：在特殊的工作负载下，LSM树的表现未必是最优的，通过利用特殊负载的性质来修改LSM树可以获得独特的更高性能
- **自动调优 Auto-Tuning**：由RUM猜想可知，不可能存在同时read-optimal、write-optimal、space-optimal的做法，因此LSM树应该根据需求调整参数以适应相应的访问方式，同时由于可调节的参数非常多，自适应调优是一种更好的方式
- **二级索引 Secondary Indexing**：LSM树只提供了针对key（索引）的操作，通常业务往往也需要能够高效处理non-key的属性，即需要二级索引

![5](images/LSM_survey5.png)

### 3.2 Reducing Write Amplification

### 3.2.1 Tiering

由于leveling策略需要频繁的merge，从而拥有更高的写放大，因此简单直白的优化方式就是直接采用tiering策略，但也会引入查询性能劣化、空间利用率下降的问题，在采用tiering策略的基础上，很多研究进一步探索了一系列**基于partitioned tiering**的LSM树变种

- **WriteBuffer (WB) Tree**
  - **partitioned tiering with vertical grouping**
  - **hash-partitioning对工作负载进行负载均衡**：从而每个SSTable组持有相近的数据量，但同时由于hash失去了对范围查询的有效支持
  - **SSTable组被组织成B+树**：利用B+树实现自平衡self-balancing**来减少总共的LSM树层数，每个SSTable组都作为B+树中的一个节点，当非叶节点的组满时（达到 $T$ 个SSTables）该组发生合并并加入到子节点对应的SSTable组中；当叶节点的组满时，该组发生合并但合并成2个组，即一半数据（ $T/2$ 个SSTables）占一个新组，作为两个新的叶节点
- **Light-weight compaction tree, LWC-tree**
  - **partitioned tiering with vertical grouping**
  - **自平衡**：由于垂直分组中的SSTables不再是固定大小的，而是在发生合并时根据下一层的分组重叠的key范围进行重新确定的大小，因此LWC树中如果一个组包含了过多的记录数（数据倾斜），就会在**合并发生后主动缩小该组（合并结束后以及为空）的key范围并扩大相邻组的key范围**，从而后续的数据持续加入该组时分流到相邻组实现自平衡，减轻数据倾斜程度
- **PebblesDB**
  - **partitioned tiering with vertical grouping**
  - **基于guards来确定key范围**：在确定组的key范围时引入了skip-list中的guard概念，即通过**对插入数据采样分析分布的概率**，从而确定key的范围实现更为均衡的分区，一旦选定了一个guard就会在下一次合并时生效，即懒惰自平衡
  - **对SSTables并行访问提升范围查询性能**
- **dCompaction**
  - **virtual SSTables和virtual merge**：一次virtual merge产生的新SSTables实际上只是包含指向多个原SSTables位置的virtual SSTables，从而**减少/推迟物理合并**的发生来提升写入性能，但会降低读取性能
  - **virtual SSTable threshold**：由于virtual SSTable指向了多个原SSTables导致了查询性能的下降，因此引入threshold要求当合并所需的真实SSTables超过该值时必须触发物理合并，相当于限制virtual SSTable指涉的真实SSTables数量，当超过时就会触发真实的物理合并
  - **read triggered merge**：当查询过程中遇到了超过一定数量的虚拟SSTabels指涉真实SSTables，也可以触发物理合并
- **SifrDB**
  - **partitioned tiering with horizontal grouping**
  - **early-cleaning during merges**：合并发生时SifrDB增量激活新创建的SSTables并且停用旧的SSTables
  - **对SSTables并行访问提升查询性能**

### 3.2.2 Merge Skipping

**skip-tree**中提出了一种合并策略来提升写入性能：通常一个记录从level 0逐级合并到level L中，如果中途可以**跳过某些level的合并**就可以减少写入次数/写入放大，从而实现更高的性能

如下图，当需要合并level L的SSTables并一直合并到level L+K时，**跳过中间的逐层合并，直接加入到level L+K的缓冲区中**，并在后续合并时参与到合并过程

![6](images/LSM_survey6.png)

为了确保正确性，**能够被直接从level L下推到level L+K的keys不能出现在level L+1到level L+K-1中，这可以通过中间层的bloom filter快速判断**，并且写入level L+K缓冲区时会通过WAL来确保可靠

跳过部分合并过程来减小写放大虽然有效，但引入了复杂的缓冲区设计以及缓冲区的WAL，综合来看skip-tree未必能够显著超过调优后的LSM树

### 3.2.3 Exploiting Data Skew

**TRIAD**提出了**冷热数据分离**的方式来减少写入放大：在有数据倾斜、部分key经常被更新的情况下，将冷热数据分离，热数据放置在内存组成部分中，只刷写冷数据到磁盘组成部分上，从而相当于内存缓存热数据

同时热数据虽然不会被刷写到磁盘上，但是TRIAD依然会周期性拷贝热数据的事务日志到新的日志中，从而允许旧日志可以被回收

另一方面TRIAD会延迟内存中level 0的合并直到有多个level 0 SSTables，从而一次性能够合并多个SSTables写入磁盘，**延迟合并提升合并数据量减少合并频率和写入放大**

TRIAD还会直接利用被废弃的旧事务日志文件作为一个磁盘组成部分，从而减少构建新的磁盘文件，通过对旧事务日志创建一个索引来加快访问

## 3.3 Optimizing Merge Operations

### 3.3.1 Improving Merge Performance

**VT-tree**提出了一种合并时的**缝合stitching**操作来提升合并性能，具体来说当某个SSTable参与合并时，若相应的数据页所包含的key范围与其他需要合并的SSTables的所有数据页包含的key都不重合时，就直接在产生的新SSTables对应的位置指向此数据页而不发生读取和拷贝，这种方式在特定的工作负载下可以提升性能，但是缺点明显，主要是：

- 可能导致**碎片化fragmentation**，不同的数据页分散在磁盘上，VT-tree进一步引入了缝合阈值stitching threshold来避免碎片化过于严重
- 合并时跳过了读取这些页的数据，从而对于合并后新产生的SSTables就**无法构建bloom filter**，VT-tree进一步引入了quotient filters来避免访问原始keys

另外也有研究者提出了采用**流水线式的合并操作充分利用CPI和I/O的并行性**来提升合并性能，主要来自于合并操作的以下三个阶段：

1. 读阶段需要从输入的SSTables中读取数据页，此时是I/O密集的
2. 合并排序阶段需要将所有读取的数据页合并并排序产生新的数据页，此时是CPU密集的
3. 写阶段需要将新数据页写入到磁盘中，此时是I/O密集的

因此当page 1第一阶段完成进入第二阶段时，就可以开始page 2的第一阶段，而不必等到page 1完全结束，从而构建处理流水线如下图（实际的系统中如RocksDB已经**通过read-ahead和write-behind实现了某种形式的流水线**）：

![7](images/LSM_survey7.png)

### 3.3.2 Reducing Buffer Cache Misses

LSM树正常运行过程中，频繁被访问的SSTables数据会被缓存，而在一次合并后，旧SSTable被回收，新生成的SSTables尚未被缓存，从而导致大量缓存失效，影响系统的查询性能

有研究者提出**增量式的缓存新数据**，当需要完成较大的合并时，将输入的组成部分发送给远程服务器进行合并，原服务器继续正常响应，当合并完成时再增量式的预热新的数据，从而避免一次性直接启用新的SSTables导致大量缓存失效，这种方式也存在一些问题：

- 组成部分需要发送给远程服务器，带来了较大的**资源开销以及延迟**
- 增量式的预热也由于**数据竞争**等问题实际效果不佳

Log-Structured buffered Merge tree, **LSbM-tree**提出了暂缓删除被合并的旧SSTables的方式来减轻缓存未命中的问题，具体来说如下图，就是在level L的SSTables合并到level L+1时，**不立即删除旧的SSTables而是加入level L+1的缓冲区**，注意参与合并并且原先已经在level L+1的SSTables不需要加入缓冲区，因为这些SSTables也同样是由level L合并生成的，数据已经在此前就加入了缓冲区，所有**缓冲区的旧SSTables基于访问频率被逐渐删除回收**，这种方式的局限在于：

- 仅对工作负载倾斜的情况比较有效，此时少量key被高频访问，假如缓冲区可以有效避免缓存未命中
- 对于冷数据而言反而引入了查询负担

![8](images/LSM_survey8.png)

### 3.3.3 Minimizing Write Stalls

相比于写入延迟稳定可控的B+树而言，LSM树的写入性能更高，但由于后台的flush和compaction服务而有**难以预见的延迟毛刺**

**bLSM**提出了一种**spring-and-gear合并调度器**来最小化写入暂停（仅针对unpartitioned leveling策略），其基本原理在于**容忍每一层存在一个额外的组成部分从而允许多个层进行并行合并**，并且合并调度器会确保只有在level L+1完成了合并操作后，level L才会合并产生level L+1的新组成部分，这种方式会级联向上（back pressure）最终反馈到内存组成部分的写入速度，这种方式的缺点有：

- 仅用于**unpartitioned leveling**策略的LSM数
- 只是限制了内存组成部分的最大写入延迟，而通常更影响性能的**排队延迟queuing latency却被忽略了**，因此最终用户侧的延迟依然有较大的不可控成分
