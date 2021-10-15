# Some notes about resharding in Seastar-based applications

Seastar采用了thread-per-shard/core的设计，在基于Seastar编写应用程序时往往也会从day 0开始[sharding all the way](Shared_Nothing.md)从而最大化这种设计的性能

传统的架构设计中往往会利用多个线程加速处理，但是底层的数据和文件则是所有线程共享并通过锁等方式来同步，从而处理的线程数可以动态（运行中或是重启）调整

而在Seastar的应用程序中，随意调整shards的数量并不容易，通常每个shard处理了一组数据和文件，一旦reshard后可能需要**重平衡rebalance**，这虽然是分布式系统下常见的策略（例如基于一致性散列consistent hashing来分区数据并重平衡），但在单机多核中实现则较为复杂，以ScyllaDB的commitlog为例：

`TODO`

`/main.cc: replaying commit log` and `/db/commitlog/commitlog_replayer.cc: db::commitlog_replayer::recover`
