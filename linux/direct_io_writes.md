# Direct I/O Writes: The Path to Storage Wealth

[original post](https://www.scylladb.com/2022/04/12/direct-i-o-writes-the-path-to-storage-wealth/)

## How Do Reads and Writes Differ ?

> **It is simply not possible to issue atomic writes to a storage device**

即使NVMe spec里提到了原子写入，但也不代表所有设备会支持，更重要的是大部分软件在设计时就要考虑会被运行在没有原子写入支持的磁盘上

大部分软件为了**避免部分写入问题，采用了日志journal**的方式，天然就是追加的模式，从而导致了两个直接后果：

- 写入存储设备的数据几乎都是**追加写入append-only**的，无论是写优化的LSM树，还是传统的B树都会采用journal的方式确保写入的数据可靠
- 通常会有一个**内存缓存memory buffer**来积累写入的数据，随后将缓存一起发送给文件

对于追加写入的数据，若是直接写入内存缓存则会非常高效，因此类似**Buffered I/O需要维护大量内存缓存**，若设备不够快可能会导致短时间内大量内存被用于缓存而OOM

大量数据仅追加写入的场景，并且很多时候这些数据并不会立即被访问到，则选择没有缓存（内存中仅有非常少的占用）的Direct I/O更为合适

## But How Much Does Direct I/O Cost ?

由于Buffered I/O仅仅是写入内存缓存即返回，因此通常会表现的非常出色，但其实**代价被转移到了随后用于执行写入磁盘的内核线程上**

- Direct I/O
  
    ```text
    user 0m7.401s
    sys 0m7.118s
    ```

- Buffered I/O

    ```text
    user 0m3.771s
    sys 0m11.102s
    ```

可以看出Direct I/O的总时间甚至比Buffered I/O要更少一些

## But Which is Faster

由于Buffered I/O真正写入磁盘持久化（`fsync`）的耗时主要在内核时间上，因此完整对比Buffered I/O和Direct I/O就需要考虑这部分时间（`Closed`时会调用`fsync`确保落盘，此时Buffered I/O就需要将内存缓存的数据真正写入磁盘）：

```text
Buffered I/O: Wrote 4.29 GB in 1.9s, 2.25 GB/s
Buffered I/O: Closed in 4.7s, Amortized total 642.54 MB/s
Direct I/O: Wrote 4.29 GB in 4.4s, 968.72 MB/s
Direct I/O: Closed in 34.9ms, Amortized total 961.14 MB/s
```

此时可以发现，考虑到真正落盘的耗时，**Direct I/O甚至明显优于Buffered I/O，并且其平均写入速度优于Buffered I/O**，更重要的是由于Direct I/O直接写入了磁盘设备，每次**写入的耗时更加可预测**，其`Close`的时间（即`fsync()`）也同样高度可预测，这种**确定性determinism延迟对于延迟敏感的任务和调度极其重要**

随着文件不断增大到超过内存，此时无法再提供相当规模的内存缓存，也需要在写入时真正写入数据，因此Buffered I/O的写入性能开始下降，而**平均写入速度和总和延迟依旧差于Direct I/O**，后者稳定保持约`960 MB/s`的写入速度（近似该测试设备的写入速度上限），并且其`Close`时间稳定在`40ms`左右

另外Buffered I/O的调用侧由于并**不知道哪一次写入操作会触发将内存缓存写入设备**，延迟出现波动，更加难以预测

```text
Buffered I/O: Wrote 17.18 GB in 10.4s, 1.64 GB/s
Buffered I/O: Closed in 11.8s, Amortized total 769.58 MB/s
Buffered I/O: Wrote 34.36 GB in 29.9s, 1.15 GB/s
Buffered I/O: Closed in 12.2s, Amortized total 814.85 MB/s
Buffered I/O: Wrote 68.72 GB in 69.4s, 989.7 MB/s
Buffered I/O: Closed in 12.3s, Amortized total 840.59 MB/s

Buffered I/O: Wrote 107.37 GB in 113.3s, 947.17 MB/s
Buffered I/O: Closed in 12.2s, Amortized total 855.03 MB/s
Direct I/O: Wrote 107.37 GB in 112.1s, 957.26 MB/s
Direct I/O: Closed in 43.5ms, Amortized total 956.89 MB/s
```

显然，在**确保数据可靠落盘的情况下，Buffered I/O的性能表现劣于Direct I/O，且存在过量使用内存作为缓存并带来OOM的风险，以及延迟波动更大稳定性不如Direct I/O**

[存储及io_uring相关的另一篇讨论见此](https://github.com/JasonYuchen/notes/blob/master/linux/io_uring.md#io_uring)
