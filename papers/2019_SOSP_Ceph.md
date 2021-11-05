# [SOSP 2019] File Systems Unfit as Distributed Storage Backends: Lessions from 10 Years of Ceph Evolution

## 1 Introduction

分布式文件系统中的存储服务器storage server通常是分布式系统整体性能的关键，其采用的**存储后端storage backend**可以有多种实现，传统实现就是基于本地文件系统local file systems例如ext4或XFS

基于本地文件系统的方案主要好处有：

- 性能尚可、数据持久性以及文件块分配等复杂工作可以直接利用成熟的本地文件系统
- 易用的POSIX接口及相应的抽象（文件、目录等）
- 允许直接使用一些标准工具来操作磁盘（`ls`，`find`等）

Ceph作为广泛被使用的开源分布式存储系统已经演尽了十年以上，采用过多种不同的本地文件系统作为存储后端，最终得出的结论是**本地文件系统并不适合作为存储后端**，除了文件系统自身高昂的额外开销导致的**性能瓶颈**以外，文件系统非常成熟以至于难以及时吸纳并支持**最新存储技术**，例如NVMe协议的SSD等设备，并且文件系统上额外支持事务等操作会带来极大的**实现复杂性**和性能惩罚

> operating systems offer all things to all people at much higher overhead

Ceph团队从2015年开始设计并实现新的存储后端**BlueStore**（2020年已经在开发基于seastar的新一代后端**SeaStore**），这种全新的后端完全处于用户态，且直接与**磁盘设备raw device**进行数据交互，**完全控制I/O栈**，支持高效的操作例如：

- 全数据校验和 full data checksums
- 内联压缩 inline compression
- 高效覆盖纠删码数据 fast overwrite of erasure-coded data

## 2 Background

### 2.1 Essentials of Distributed Storage Backends

分布式文件系统运行在多个物理机器上并对外表现为单个统一的存储，拥有高带宽、并行I/O、水平扩容、容错、强一致等特性，而其通常在每个物理节点上会运行**存储后端storage backend**程序来直接管理物理设备，存储后端要求：

- **高效事务 efficient transactions**
  通过事务可以轻易的支持强一致性，但是基于本地文件系统（POSIX标准并没有事务概念）的传统存储后端需要更高的代价来支持事务，例如实现WAL机制等
- **高效元数据操作 fast metadata operations**
  传统文件系统往往对大目录（海量目录项）及小文件的支持不佳，元数据管理性能劣化严重，导致存储后端需要是用诸如元数据缓存、深目录层级、数据库等方式管理元数据
- **对新硬件设备的支持 support for novel, backward-incompatible storage hardware**
  一些新的硬件设备引入了不兼容的修改，导致传统的文件系统难以及时这些新的设备，从而无法充分发挥出设备应有的性能，例如HDD的SMR技术、SSD的ZNS技术都可以显著提高硬件性能，但尚未被传统本地文件系统充分支持

### 2.2 Ceph Distributed Storage System Architecture

Ceph分布式存储系统的架构如下图所示：

- Ceph的核心是**Reliable Autonomic Distributed Object Store, RADOS服务**，RADOS可以支持扩容到数千个**Object Storage Devices, OSDs**，并支持自动修复、自动管理、多副本强一致访问特性，在此之上的librados提供了对象操作的事务接口
- 基于RADOS及librados，Ceph实现了三种存储服务，分别为：
  - **对象存储 RADOS Gateway, RGW**，类似AWS S3
  - **块存储 RADOS Block Device, RBD**，类似AWS EBS
  - **文件存储 CephFS**，支持POSIX的分布式文件系统
- 对象被存储在RADOS的Pool中，**pools是可以提供数据冗余（基于副本replication或纠删码erasure coding）的逻辑分区**，在一个pool中对象被分片存储到一组**聚集单元placement groups, PGs**内，基于配置的副本数，PGs再**基于[CRUSH](https://access.redhat.com/documentation/en-us/red_hat_ceph_storage/1.2.3/html/storage_strategies/introduction_to_crush)算法**被映射到相应数量的OSDs上（访问数据的客户端同样采用CRUSH算法来确定OSDs的位置，从而避免了中心化的元数据服务），PGs以及CRUSH算法组成了客户端和OSDs的中间层，从而允许OSDs可以在客户无感知的情况下进行迁移、负载均衡等操作
- 每个RADOS集群节点的每个存储设备都会有一个独立的**Ceph OSD daemon**进程，而每个OSD与其他peer OSDs节点一起完成客户端的I/O请求、数据复制、纠删码更新、数据迁移、故障恢复等任务
- OSD节点通过实现了**ObjectStore接口**的后端存储在本地管理所有数据，该接口提供了对象的抽象、对象集合、操作指令、事务操作（一个事务操作可以包括任意对象和操作指令，并保证了事务操作的原子性），Ceph集群中每个OSD可以使用不同的后端存储实现，只要实现了ObjectStore接口即可

![ceph1](images/ceph1.png)

### 2.3 Evolution of Ceph's Storage Backend

Ceph的存储后端有过多年演变：

1. **EBOFS**: Extent and B-Tree-based Object File System
   EBOFS实际上是用户态文件系统
2. **FileStore/Btrfs**
   基于本地文件系统Btrfs的FileStore，Btrfs支持事务、去重、校验和、透明的压缩，极大的拓展了EBOFS的功能

   在FileStore下，一个对象集合被映射为一个目录，对象的数据就存储在文件中，初始实现中对象的属性保存在POSIX extended file attributes, xattrs中，随后改用了LevelDB来存储（属性数量和大小超出xattrs的限制）

   采用Brtfs期间，Brtfs始终不稳定且易出现数据和元数据的碎片化，并且ObjectStore接口本身也在大量修改到不再兼容EBOFS，随后FileStore就开始采用XFS、ext4、ZFS等其他文件系统，其中主流做法就是基于XFS
3. **FileStore/XFS**
   基于本地文件系统XFS的FileStore表现出了更好的可扩展性以及元数据处理性能，但是XFS也没有完全避免元数据碎片化，同时XFS并不支持事务，且不能及时跟上硬件设备的发展，无法充分挖掘硬件设备本身的性能

   缺乏对事务的支持导致FileStore在用户层实现了WAL机制来记录全数据的日志，使得Ceph中常见的read-modify-write任务的速度受限于WAL写入文件的速度，另一方面XFS并不是copy-on-write的文件系统，使得Ceph中的快照操作（会进行clone）显著变慢
4. **NewStore**
   首次尝试解决元数据的问题，NewStore将对象元数据储存在RocksDB中，而对象数据依然存储在文件中，并且WAL也采用RocksDB来实现，从而使得read-modify-write的操作性能更好（得益于RocksDB合并管理数据和元数据日志）

   这种实现引入高昂的一致性代价（？），并催生了BlueStore，直接管理底层存储设备raw disks
5. **BlueStore**
   见后续详细分析
6. **SeaStore**
   在本文（2019）中并没有体现，实际上是从2020年开始基于Seastar开发的下一代存储后端，`TODO: 分析SeaStore希望解决的问题`

![ceph2](images/ceph2.png)

## 3 Building Storage Backends on Local File Systems is Hard
