# CMU 14-445 Database Systems (Fall 2020)

[course schedule](https://15445.courses.cs.cmu.edu/fall2020/schedule.html)

With extra materials from cs186 (UCB) and comp7104 (HKU)

## Projects

1. **C++ Primer**
   - Matrix ✅
   - Matrix Operation ✅
2. **Buffer Pool Manager**
   - LRU Replacement Policy ✅
   - Buffer Pool Manager ✅
3. **B+ Tree Index**
   - B+ Tree Pages ✅
   - B+ Tree Data Structure ✅
   - Index Iterator ✅
   - Concurrent Index ✅
4. **Query Execution**
   - System Catalog ✅
   - Executors ✅
5. **Concurrency Control**
   - Lock Manager ✅
   - Deadlock Detection ✅
   - Concurrent Query Execution ✅

具体踩坑过程见此[记录](Project.md)，遵守CMU的课程要求，源码不开放

## Lecture 01 - Course Introduction and the Relational Model

- relational model
- relational algebra

[here](01.Relational.md)

## Lecture 02 - Advanced SQL

- aggregate
- nested queries
- window function
- common table expression

[here](02.Advanced_SQL.md)

## Lecture 03 - Database Storage I

- file storage
- page layout
- tuple layout

[here](03.Storage_I.md)

## Lecture 04 - Database Storage II

- storage model: OLTP/OLAP/HTAP

[here](04.Storage_II.md)

## Lecture 05 - Buffer Pools

- buffer pool
- replacement policies

[here](05.Buffer_Pools.md)

## Lecture 06 - Hash Tables

- static hashing
- dynamic hashing

[here](06.Hash_Tables.md)

## Lecture 07 - Trees Indexes I

- B+ tree

[here](07.Tree_Indexes_I.md)

## Lecture 08 - Trees Indexes II

- B+ tree
- More Index Considerations

[here](08.Tree_Indexes_II.md)

## Lecture 09 - Index Concurrency Control

- Lock and Latch
- Concurrent Index Operations

[here](09.Index_Concurrency_Control.md)

## Lecture 10 - Sorting + Aggregations

- multi-pass sorting
- sorting aggregation
- hashing aggregation

[here](10.Sorting_Aggregation.md)

## Lecture 11 - Joins Algorithms

- loop join
- sort-merge join
- hash join

[here](11.Join_Algorithms.md)

## Lecture 12 - Query Execution I

- processing models
- accessing methods

[here](12.Query_Execution_I.md)

## Lecture 13 - Query Execution II

- query parallelism
- I/O parallelism

[here](13.Query_Execution_II.md)

## Lecture 14 - Query Planning & Optimization I

- relational algebra equivalences
- logical plan optimization

[here](14.Query_Planning_I.md)

## Lecture 15 - Query Planning & Optimization II

- cost-based query planning
- plan enumeration

[here](15.Query_Planning_II.md)

## Lecture 16 - Concurrency Control Theory

- ACID

[here](16.Concurrency_Control.md)

## Lecture 17 - Two-Phase Locking Concurrency Control

- 2PL
- deadlock detection & prevention

[here](17.Two_Phase_Locking.md)

## Lecture 18 - Timestamp Ordering Concurrency Control

- timestamp ordering
- optimistic concurrency control
- isolation level

[here](18.Timestamp_Ordering.md)

## Lecture 19 - Multi-Version Concurrency Control

- version storage
- garbage collection
- index management

[here](19.MVCC.md)

## Lecture 20 - Logging Protocols + Schemes

- write-ahead log, WAL
- checkpoint/snapshot

[here](20.Logging.md)

## Lecture 21 - Crash Recovery Algorithm

- ARIES

[here](21.Recovery.md)

## Lecture 22 - Introduction to Distributed Databases

- parallel vs. distributed
- system architectures
- partitioning schemes

[here](22.Distributed.md)

## Lecture 23 - Distributed OLTP Database Systems

- atomic commit
- replication
- consistency

[here](23.Distributed_OLTP.md)

## Lecture 24 - Distributed OLAP Dataase Systems

- execution model
- query planning
- distributed join

[here](24.Distributed_OLAP.md)

## Lecture 26 - Final Review / Other Systems

- Amazon DynamoDB
- Cassandra
- mongoDB
- review

[here](25.Final_Review.md)
