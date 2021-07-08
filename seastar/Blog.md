# A collection of seastar's blog articles

## Seastar Overview

[Seastar: The `future<>` is Here](https://www.scylladb.com/2018/01/04/seastar-futures/)

[notes](https://github.com/JasonYuchen/notes/blob/master/seastar/Introduction.md)

## Benchmark a database

[Best Practices for Benchmarking Scylla](https://www.scylladb.com/2021/03/04/best-practices-for-benchmarking-scylla/)

- set throughput target
- how to measure latency correctly
- number of threads
- number of connections

[notes](BenchmarkDB.md)

## Userspace File System

`TODO`

Use seastar to implement a
[userspace file system](https://www.scylladb.com/2020/08/25/scylla-student-projects-part-ii-implementing-an-async-userspace-file-system/)

- SeastarFS: not a general-purpose, POSIX-compliant file system
- Assumptions:
  - handle large files mostly
  - no hot files or dirs, mostly no conflicts among users (shared nothing)
  - sequential r/w mostly
- Log-structured

## Parquet Support

`TODO`

Use seastar to support
[parquet](https://www.scylladb.com/2020/08/05/scylla-student-projects-part-i-parquet/)

- Parquet: a well known columnar storage format
- Implement parquet using seastar from scratch based on its documentation

## io_uring and eBPF

[How io_uring and eBPF will revolutionize programming in Linux](https://www.scylladb.com/2020/05/05/how-io_uring-and-ebpf-will-revolutionize-programming-in-linux/)

- Linux syscall evolution
- io_uring vs. AIO

[notes](https://github.com/JasonYuchen/notes/blob/master/linux/io_uring.md)

## Core C++ 2019

[Avi Kivity at Core C++ 2019](https://www.scylladb.com/2020/03/26/avi-kivity-at-core-c-2019/)

- Seastar introduction
- Shard per logical core arch
- Futures, promises, continuations

[notes](https://github.com/JasonYuchen/notes/blob/master/seastar/Message_Passing.md)

## Seastar Summit 2019

`TODO`

[Seastar Summit 2019](https://www.scylladb.com/2019/09/03/seastar-summit-2019/)

## Server based on ARM

[Is Arm ready for server dominance?](https://www.scylladb.com/2019/12/05/is-arm-ready-for-server-dominance/)

The performance of the Arm-based server is comparable to the x86 instance !

## Database Partitioning and Replication

`TODO`

[Making a Scalable and Fault-Tolerant Database System: Partitioning and Replication](https://www.scylladb.com/2020/10/20/making-a-scalable-and-fault-tolerant-database-system-partitioning-and-replication/)

- Why & how partitioning and replication
- Scalability & fault-tolerance

## Database Benchmark

`TODO`

[How to Test and Benchmark Database Clusters](https://www.scylladb.com/2020/11/04/how-to-test-and-benchmark-database-clusters/)

- Develop Tangible Targets
- Testing Schedule
- Data Pattern
- Stress Testing
- Disaster Recovery Testing
- Observability

## I/O Scheduler

A userspace disk I/O scheduler in seastar

- Stage 1 (-2021.01.28)
  - [part 1](https://www.scylladb.com/2016/04/14/io-scheduler-1/)
  - [part 2](https://www.scylladb.com/2016/04/29/io-scheduler-2/)
- Stage 2 (2021.01.28-, [Project Circe](https://www.scylladb.com/2021/01/28/project-circe-january-update/))
  - [blog](https://www.scylladb.com/2021/04/06/scyllas-new-io-scheduler/)

[notes](https://github.com/JasonYuchen/notes/blob/master/seastar/Disk_IO_Scheduler.md)

## Memory Barriers when `wake_up`

[Memory Barriers and Seastar on Linux](https://www.scylladb.com/2018/02/15/memory-barriers-seastar-linux/)

Performance improvement when conditionally `wake_up` other threads, an interesting insight!

[notes](https://github.com/JasonYuchen/notes/blob/master/seastar/Membarrier_Adventures.md)

## Incremental Compaction

`TODO`

[Maximizing Disk Utilization with Incremental Compaction](https://www.scylladb.com/2020/01/16/maximizing-disk-utilization-with-incremental-compaction/)

[A Revolutionary Space and Write Optimized Compaction Strategy](https://www.scylladb.com/2021/04/28/incremental-compaction-2-0-a-revolutionary-space-and-write-optimized-compaction-strategy/)

## On Coordinated Omission

[On Coordinated Omission](https://www.scylladb.com/2021/04/22/on-coordinated-omission/)

[notes](Coordinated_Omission.md)
