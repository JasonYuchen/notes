# 如何测试Scylla

[original post](https://www.scylladb.com/2021/03/04/best-practices-for-benchmarking-scylla/)

## 指标

对于数据库来说，通常有以下一些指标需要在测试中考虑：

- **吞吐量throughput/负载load**：每秒的请求处理量 Operations Per Second, OPS
- **延迟latency**：一个请求从发出，到接收到数据库的响应，经过的时间称为延迟，对OLTP类的数据库来说，最核心的测试要点就在于找到吞吐量-延迟曲线
- **P99**：在一批请求中，99%的请求的延迟最大值，描述延迟的分布情况，后1%的延迟也称为**长尾延迟long-tail latency**
- **利用率utilization**：数据库不可能处理无上限的负载，通常吞吐量越高则延迟越高，一个数据库当前的处理量/最大处理量称为利用率

## 目标与负载

通常对于OLTP类的数据库，延迟至关重要，可以首先将测试的目标设置为P99=10ms，随后根据该目标生成工作负载，常见的开源框架有

- [Cassandra-stress](https://github.com/scylladb/scylla-tools-java/tree/master/tools/stress)
- [YCSB](https://github.com/brianfrankcooper/YCSB)
- [TPC-C](http://tpc.org/tpcc/default5.asp)
- [TLP-stress](https://github.com/thelastpickle/tlp-stress)

测试的流程如下：

1. **设定目标**，例如希望系统能够处理X吞吐量，保证Y的P99延迟，以及利用率是Z，总数据量是D
2. **尽可能生成贴近实际情况的负载**，考虑是read-intensive、write-intensive、mixed等情况
3. 理解不同的负载对系统的影响，并且可以预判例如CPU压力大、内存不足、磁盘响应慢、网络带宽不够等
4. 选择数据库并进行初始配置
5. 通常被测试的系统以及数据加载组件不应该在同一集群中使用，显然数据加载组件会影响测试系统的性能
6. 在测试时需要同时观测被测系统以及测试系统的负载，**如果测试系统首先过载，则显然不能测出被测系统的性能**
7. **正确的生成负载，考虑[coordinated omission问题](http://highscalability.com/blog/2015/10/5/your-load-generator-is-probably-lying-to-you-take-the-red-pi.html)**
   当系统因为某个请求卡住时阻塞了客户端，此时客户端后续的测试请求都没有发出，等到真的发出时再记录发出时间和响应时间，就**遗漏了因前面阻塞而等待的时间**

   引入**意向时间intended time**来记录每个请求实际应该被发出的时间作为起始时间，显然系统没有阻塞处理良好时请求的意向时间与发起时间相等；而当阻塞时，后续请求的意向时间就会包括了阻塞等待时间，更贴近实际情况
8. **测量延迟的分布情况**，平均值毫无意义
9. 根据延迟情况与设定的目标，调整系统配置，并确保测试结果可复现
