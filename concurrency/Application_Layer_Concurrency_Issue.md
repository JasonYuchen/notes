# Towards Application Layer Concurrency Issue

## 一种应用层场景

假定一条请求需要两次数据库Read-Modify-Write操作涉及两行数据，并且第二行数据的修改会依赖第一行数据的修改：

1. `recv(request)`
2. `read(A)`
3. `result_A = compute(A, request.operation)`
4. `write(result_A)`
5. `read(B)`
6. `result_B = compute(B, result_A)`
7. `write(result_B)`
8. `send(response)`

将任何一次请求的处理所涉及的所有数据库操作都视为一次事务，则并发多条请求的情况下就等同于事务的[并发处理 Concurrency Control](https://github.com/JasonYuchen/notes/blob/master/cmu15.445/16.Concurrency_Control.md#lecture-16-concurrency-control-theory)，与多线程的并发也有类似之处，并且为了服务的性能和吞吐量，所有请求的处理都会大量依赖并发

## 为什么我们需要并发处理？

- 大部分情况下很多请求涉及到的数据并不相同，因此一个线程在处理一个请求并等待其IO操作时，CPU完全可以继续处理另一个请求，待IO操作完成时再切换回来，从而显著提升处理效率
- **[硬件发展](https://github.com/JasonYuchen/notes/blob/master/seastar/Shared_Nothing.md#%E7%A1%AC%E4%BB%B6%E5%8F%91%E5%B1%95-hardware-evolution)**，多核处理器已经越来越普遍，并发计算可以充分利用多核
- **成本与性能：吞吐量、延迟**

## 并发处理的危险之处

- **[数据竞争](https://github.com/JasonYuchen/notes/blob/master/concurrency/cpp/03.Sharing_Data_Between_Threads.md#%E7%BA%BF%E7%A8%8B%E6%95%B0%E6%8D%AE%E5%85%B1%E4%BA%AB%E9%97%AE%E9%A2%98-problems-with-sharing-data-between-threads)**
  以上述场景为例，假如有两个请求同时进行处理，那么有可能出现：
  - **丢失更新 lost update**：请求1和请求2均对数据A进行读取，分别计算，并且尝试写入结果，此时后写入的请求保留了结果，先写入的请求结果被覆盖（即**最终写者获胜 last writer win**），但客户端依然能收到成功的反馈
  - **数据不一致 inconsistency**：在前述的基础上，随后两个请求各自读取数据B并根据不同的数据A计算结果（例如请求1的数据A结果实际上被请求2覆盖了，但请求1依然采用自己的数据执行后续流程）进行数据B的计算，此时可能出现在数据库中的数据A是请求2的写入结果，而数据B是请求1的写入结果
- **回顾[数据库ACID](https://github.com/JasonYuchen/notes/blob/master/ddia/07.Transactions.md#%E5%BC%B1%E7%9A%84%E9%9A%94%E7%A6%BB%E7%BA%A7%E5%88%AB-weak-isolation-levels)与弱隔离级别带来的问题**
  - 脏读 dirty read
  - 脏写 dirty write
  - 读偏差 read skew/non-repeated read
  - 写偏差 write skew
  - 幻读 phantom
- 如何实现（分布式）锁？
  `TODO`
- 如何检测（分布式系统）死锁？
  `TODO`

## 依赖RDBMS的无状态应用层

采用RDBMS的事务支持就可以很简单的回避这种竞争问题：

1. `recv(request)`
2. `txn.begin()`
3. `RMW(A)`
4. `RMW(B)`
5. `txn.commit()`
6. `send(response)`

显然根据事务的要求，假如出现并发读取A和B的情况，两个请求会被串行化执行，并不会出现事务1更新A的时候事务2就读取到了未提交的结果，**在未涉及相同数据行的时候，并发可以良好的进行，事务之间没有冲突；当涉及相同数据行的时候，由RDBMS来确保事务之间的隔离性，应用层不必为此付出额外的处理**，仅仅只需要根据事务的提交结果（提交commit/回滚abort）相应返回

## 更高性能的NoSQL与陷阱

当数据量和请求量大到一定程度时，[单机的RDBMS](https://www.mysql.com/why-mysql/benchmarks/mysql/)可能已经无法支撑这样的流量（暂时不讨论基于分布式中间件的分库分表RDBMS集群、或是对事务有良好支持的NewSQL系统），此时可能会选择**扩展性极佳、性能优越**的NoSQL系统，例如[HBase](https://hbase.apache.org/)

- **SQL与NoSQL**
  NoSQL带来的杰出性能不是没有代价的，通常NoSQL的设计充分考虑了海量数据以及高并发场景，可扩展性极佳，代价就是舍弃了RDBMS的事务支持程度，没有完善的事务支持（NoSQL通常支持非常有限的事务），应用层往往就会面临上述场景数据并发的问题，没有事务的支持也就意味着**出现竞争时需要由应用层自己来处理冲突的情况**
- **HBase对ACID的支持**
  - HBase支持[CAS](https://en.wikipedia.org/wiki/Compare-and-swap)风格的行级原子操作，也可以认为是[行级事务](https://hbase.apache.org/acid-semantics.html)，而不支持跨行事务

    > 1. All mutations are atomic within a row.
    > 2. APIs that mutate several rows will _not_ be atomic across the multiple rows.

## 回到应用层场景并拆分事务操作

- **采用行级事务完全解决丢失更新问题**
  每一次写入均采用CAS的方式，从而一旦CAS失败说明检测到了并发写入，此时就应该视业务要求，直接返回错误，或者重新读取数据再做计算并重试CAS，采用这种方式可以有效避免覆盖了其他进程的写入结果
- **谨慎处理每一行的操作来尽可能保证跨行数据的一致**
  对于跨行数据，由于没有事务和CAS的支持，本质上非常难以保证一致性，只能通过具体的业务情况设计不同的失败策略，例如只有两行数据的写入，若第一行写入失败就不进行第二行的写入；若第二行写入失败就考虑回滚第一行的写入或者是通过异步任务进行第二行的补偿（也可能会进一步劣化不一致的程度）
  
  对于更多行数据的写入其一致性的维护难度就会显著增加，可以考虑业务层的拆分，尽可能避免跨行的操作，必须跨行操作则考虑尽可能少的操作
- **最不利情况下可以考虑的做法**
  采用一些保底策略，例如锁住整个数据表并进行一致性校验和维护等，代价比较大，需要谨慎，这一方面也应该充分考虑每个数据表的尺寸

  **采用支持事务的数据库**

## 如何测试这种场景的并发安全性？

- **针对行级操作的并发测试**
  通过将业务操作拆分成每个数据库的读取和写入，则可以手动调度并发请求的数据库操作执行顺序，如下图，从而可以**实现确定性的执行顺序交织**，从而暴露并发处理的问题
- **协调行级操作的顺序来模拟多行操作的并发下表现**
  例如一个请求的处理包含两次数据库操作，每一次数据库操作都是原子的CAS操作，那么理论上其调度可以有以下三种情况：
  
  ```text
  ----case #1---------------------------->
  req1     RMW(A) RMW(B)
  req2                   RMW(A) RMW(B)
  ----case #2---------------------------->
  req1     RMW(A)        RMW(B)
  req2            RMW(A)        RMW(B)
  ----case #3---------------------------->
  req1     RMW(A)               RMW(B)
  req2            RMW(A) RMW(B)
  ```

  在构建测试时就可以手动按上述顺序调用数据库操作，来模拟并发处理的情形，从而对单个请求的处理逻辑进行测试，这种手动构建顺序的测试依赖于数据库的原子操作作为最小测试单元，并且能够**确定性的复现多请求并发处理下可能出现的所有时序**，缺点在于随着原子操作数量的增加，可能出现的情况会指数级增加
