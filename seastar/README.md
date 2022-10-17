# Seastar

- Seastar in action
- Seastar source code insights
- Various interesting technical blogs from Scylla team

## Introduction

1. [Introduction](Introduction.md)

1. [Comprehensive tutorial in Chinese](Comprehensive_Tutorial.md)

1. [Build & test seastar](Setup.md)

1. [Shared-nothing architecture](Shared_Nothing.md)

1. [Technical blogs about scylladb/seastar](Blog.md)

## Insight

1. [Actor in seastar](Message_Passing.md)

1. [How coroutine fits into future/promise](Coroutines.md)

1. [Coroutine lambda pitfalls](Coroutine_Lambda_Pitfall.md)

1. [Simple coroutine queue](Queue.md)

1. [Dedicated file stream](FStream.md)

1. [Reactor engine](Reactor.md)

1. [Coordinated omission](Coordinated_Omission.md)

## Advanced topics

1. [Memory barrier in producer & consumer](Membarrier_Adventures.md)

1. [User space disk I/O scheduler](Disk_IO_Scheduler.md)

1. [Advanced User space disk I/O scheduler](New_Disk_IO_Scheduler_For_RW.md)

1. [Memcache](Memcached.md)

1. [How to benchmark a database system](BenchmarkDB.md)

1. [How to bridge GTest and Seastar](Unittest.md)

1. [How to change shards after rebooting? Example from Scylla](Reshard.md)

1. [How to control your system with feedback? Dynamic priority adjustment](Dynamic_Priority_Adjustment.md)

## Other related notes

1. [io_uring + Seastar](https://blog.k3fu.xyz/seastar/2022/10/03/iouring-seastar.html): How to use `io_uring` for network IOs and what's the **difference between network IOs and disk IOs**?
