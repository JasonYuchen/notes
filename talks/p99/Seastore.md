# Seastore: Next Generation Backing Store for Ceph

## Emerging Technologies

- **ZNS**
  - 传统FTL设计会引入高的写入放大，并且后台垃圾回收会影响到前端请求的尾延迟
  - ZNS采用不兼容的新接口，提供Zones作为写入接口，Zones只能打开、顺序写入、关闭和释放，更加贴近闪存块的本质
- **Persistent Memory**
  - 断电时写入不会丢失，且读请求的延迟接近DRAM，写请求的延迟优于闪存块
  - 特性非常适合存储缓存数据和元数据
  - SeaStore目前希望将缓存层完整的放置在Persistent Memory中，采用Copy-On-Write的方式更新数据，并且采用Write-Ahead-Log

## SeaStore

```text
-------------------------------------------------------------
|                        SeaStore                           |
-------------------------------------------------------------
|  OnodeManager  |  OmapManager  |  ObjectDataHandler | ... |
-------------------------------------------------------------
|                    TransactionManager                     |
-------------------------------------------------------------
| Cache | Journal | LBAManager | SegmentCleaner | RootBlock |
-------------------------------------------------------------
```

- **TransactionManager**：提供了数据块和元数据的逻辑地址事务操作接口，例如分配、读取、修改逻辑地址寻址的数据块，而在TransactionManager层以下的组件则是用于处理物理地址，在该层以上的组件则对底层的垃圾回收等过程完全透明（`crimson/os/seastore/TransactionManager`）
- **OnodeManager**：即FLTree，类似BTree的数据结构，维护Ceph对象的元数据（`crimson/os/seastore/onode_manager/staged_fltree`）
- **OmapManager**：即BteeOmapManager，类似BTree的数据结构，维护每个对象的omap storage（`crimson/os/seastore/omap_manager/btree`）
- **ObjectDataHandler**：（`crimson/os/seastore/object_data_handler`）
- **LBAManager**：即BtreeLBAManager，维护逻辑地址到物理地址的映射，并且通过引用计数的方式支持clone操作（`crimson/os/seastore/lba_manager/btree`）
- **Cache**：管理内存中的数据块、脏块，并且会检测将要提交的事务中的冲突（`crimson/os/seastore/lba_manager/cache`）
- **SegmentCleaner**：维护segment的使用情况，并且通过运行后台任务执行垃圾回收（`crimson/os/seastore/lba_manager/segment_cleaner`）

展示内容有限，需要对ceph有更深入的理解
