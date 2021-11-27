# [SC 2006] CRUSH: Controlled, Scalable, Decentralized Placement of Replicated Data

## 1 Introduction

CRUSH算法是一种伪随机数据分布算法，目标在于高效、稳健的将海量对象数据均匀可靠的复制到同构的存储集群中

通常只需要提供给CRUSH算法一个对象号，就可以生成一组需要存储该对象副本的存储设备列表，CRUSH算法是确定性的deterministic，并且不依赖中心目录节点，其仅需要一个所有存储设备的**层级描述hierarchical description**，以及相应的副本**放置策略replica placement policy**

这种设计使得CRUSH算法有两个显著的优点：

1. CRUSH算法是完全分布式的，集群中的任意节点都可以独立求出一个对象的副本位置
2. 仅需要极少数的元数据，这些数据仅在存储设备被添加/删除时才需要更新

另一方面，当存储设备有增减时，CRUSH算法也会高效的进行**重平衡rebalance**优化存储结构，尽可能充分利用每个存储节点的资源，避免少数节点过载

## 2 Related Work

`TODO`

## 3 The CRUSH algorithm

CRUSH算法将数据基于存储设备权重值（每个设备都有一个权重）进行均匀分布，分布过程受**层级化的集群图cluster map**，并且数据**放置策略placement rules**也会参与计算放置副本的设备，例如要求存储3个副本且这3个副本所在的机架不能共享电源

### 3.1 Hierarchical Cluster Map

集群图由**设备devices**和**桶buckets**组成，这两者都有相应的权重值，**桶作为集群图的中间节点**可以包含任意数量的设备或其他桶，而**设备总是叶节点**，每个设备都由集群管理员分配权重值，代表着该设备将会负责的数据量，而桶的权重值就是所包含的所有子节点的权重值之和

传统的散列算法来分配数据，会出现存储节点的增减导致大量数据需要被重新分配到其他节点上（其中一种解决方案是采用**一致性散列consistent hashing**），而CRUSH算法基于四种不同的桶类型，尽可能的减少因节点增减导致的数据移动

### 3.2 Replica Placement

CRUSH算法对底层物理设备的建模使得CRUSH能够**感知到底层存储设备的故障相关性**，例如共享电源或共享网络的设备，从而在执行数据分配时就会选择不在相同**故障域failure domain**的存储设备来存储多个副本

- CRUSH的输出参数`x`往往是一个对象名，或者是一组对象的识别号，从而这一组对象都会从CRUSH计算出相同的存储位置
- `TAKE(a)`将会从存储层次种选择一个节点（通常是桶节点bucket）并赋给向量`i`作为后续过程的输入
- `SELECT(n, t)`迭代每一个属于向量`i`的节点，并选择`n`个独立且类型为`t`的节点
- `SELECT(n, t)`在迭代时将会递归下降进入所有中间节点，采用伪随机的方式`c(r, x)`选择一个类型符合`t`的节点，`c(r, x)`对不同的桶类型都有不同的定义，见3.4节
- `EMIT`将结果输出
- 每个存储设备都有一个已知且固定的类型，每个桶也有一个类型字段用来区分不同类型的桶

![crush1](images/crush1.png)

例如以下过程：

1. 从`root`开始，`TAKE(root)`返回根节点`root`自身并作为`SELECT`的输入
2. 选择单个桶节点且类型为`row`，伪随机选出`row2`，递归进一步选择`row2`下的三个桶节点
3. 从`row2`桶下伪随机选出`cab21 cab23 cab24`三个桶节点且类型为`cabinet`，递归进一步选择每个`cabinet`下的一个节点
4. 从`cab21`下选出`disk2107`，同理获得三个存储设备节点且类型为`disk`
5. 结束，输出结果`disk2107 disk2313 disk2437`，显然这三个位置属于同一个`row`但分属于三个`cabinet`，类似的做法就可以确保副本不属于相同的故障域

|Action|Resulting Vector `i`|
|:-|:-|
|`TAKE(root)`|root|
|`SELECT(1, row)`|row2|
|`SELECT(3, cabinet)`|cab21 cab23 cab24|
|`SELECT(1, disk)`|disk2107 disk2313 disk2437|
|`EMIT`||

![crush2](images/crush2.png)

#### 3.2.1 Collisions, Failure, and Overload

`SELECT(n, t)`操作需要遍历存储层次的多个节点来确定`n`个独立的`t`类型节点，而在过程中CRUSH算法会处于以下三个理由拒绝并基于修改后的范围重新选择：

1. **冲突 collision**：当前节点已经被选择
2. **故障 failure**：当前节点被标记为故障
3. **过载 overload**：当前节点被标记为过载

故障和过载节点都不会从集群图中移除，而只是被标记了相应的状态，从而避免不必要的数据迁移，并且**CRUSH会选择性的挑选过载节点的一部分数据，基于伪随机拒绝来进行负载均衡**

对于故障和过载设备，CRUSH会通过重新执行算法第11行`SELECT(n, t)`（在第29行设置`retry_descent = true`）并被伪随机拒绝来均匀的重新分布相应的数据，对于冲突节点，CRUSH会通过算法第14行采用新的`r'`重新执行`b.c(r', x)`尝试本地搜索（在第27行设置`retry_bucket = true`）

#### 3.2.2 Replica Ranks

平等备份和纠删码备份有不同的副本放置策略，在主备模式下，通常一个主副本失效后就应该选择一个备份成为新的主副本，此时CRUSH就可以采用**前n个适用目标"first n"策略**，通过`r' = r + f`来修正，其中`f`代表了当前一次`SELECT(n, t)`失败的数量

而纠删码模式下每个存储设备都存储了数据的一部分因此不能简单的直接选择其他副本，不同的副本并不是完全对等的（有**级别rank**的概念），需要**通过`r' = r + f_r * n`来选择副本**（实际上是确定了一个副本候选序列），其中`f_r`代表了在`r`这一副本上失败的次数

![crush3](images/crush3.png)

### 3.3 Map Changes and Data Movement

### 3.4 Bucket Types

#### 3.4.1 Uniform Buckets

#### 3.4.2 List Buckets

#### 3.4.3 Tree Buckets

#### 3.4.4 Straw Buckets

### 4 Evaluation

### 5 Future Work

`TODO`

### 6 Conclusions

`TODO`
