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
