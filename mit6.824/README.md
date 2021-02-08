# MIT 6.824 Distributed Systems Engineering

[course schedule](https://pdos.csail.mit.edu/6.824/schedule.html)

## Lecture 01 - Introduction

What is a distributed system?

Why do people build distributed systems?

Why take this course?

**CASE STUDY: MapReduce**

- [lecture notes](01.Introduction.md)
- [material notes](MapReduce.md)

## Lecture 02 - RPC and Threads

Why threads?

What is Remote Procedure Call (RPC)?

- [lecture notes](02.RPC_threads.md)
- material notes

## Lecture 03 - GFS

Why is distributed storage hard?

What would we like for consistency?

**CASE STUDY: The Google File System**

- [lecture notes](03.GFS.md)
- [material notes](GFS.md)

## Lecture 04 - Primary-Backup Replication

What kinds of failures can replication deal with?

How to replicate? State transfer or Replicated state machine?

**CASE STUDY: VMware Fault-Tolerant Virtual Machines**

- [lecture notes](04.Primary_Backup.md)
- [material notes](Fault_Tolerant_VM.md)

## Lecture 05 - Go, Threads and Raft

- [lecture notes](05.Go_threads_Raft.md)
- [material notes](https://github.com/JasonYuchen/notes/tree/master/raft)

## Lecture 06 - Fault Tolerance: Raft (1)

How coulds split brain arise, and why is it damaging?

Why a leader?

Why the logs?

**CASE STUDY: Raft (elections and log handling)**

- [lecture notes](06.Raft_Election_and_Log.md)
- [material notes](https://github.com/JasonYuchen/notes/tree/master/raft)

## Lecture 07 - Fault Tolerance: Raft (2)

How can logs disagree after a crash?

What would we like to happen after a server crashes?

What is linearizability? **[Dive into consistency models](https://github.com/JasonYuchen/notes/tree/master/consistency)**

What should a client do if a Put or Get RPC times out? How about the server?

**CASE STUDY: Raft (persistence, client behavior, snapshots)**

- [lecture notes](07.Raft_Log.md)
- [material notes](https://github.com/JasonYuchen/notes/tree/master/raft)

## Lecture 08 - Zookeeper

Can replicas serve read-only client requests form their local state?

If so, how about the consistency guarantees?

Why is ZooKeeper useful despite loose consistency?

**CASE STUDY: ZooKeeper**

- [lecture notes](08.ZooKeeper.md)
- [material notes](ZooKeeper.md)

## Lecture 09 - More Replication: CRAQ

What is Chain Replication (CR)? What is CRAQ?

Why is CR attractive (vs Raft)? How about CRAQ?

**CASE STUDY: CRAQ**

- [lecture notes](09.CRAQ.md)
- [material notes](CRAQ_Chain_Replication.md)

## Lecture 10 - Cloud Replicated DB: Aurora

What led to Aurora?

Why Aurora is successful?

What does a Aurora read/write look like? Quorum?

**CASE STUDY: Aurora**

- [lecture notes](10.Aurora.md)
- [material notes](Aurora.md)

## Lecture 11 - Cache Consistency: Frangipani

cache coherence, distributed transaction, distributed crash recovery

**CASE STUDY: Frangipani**

- [lecture notes](11.Frangipani.md)
- [material notes](Frangipani.md)

## Lecture 12 - Distributed Transactions

What is ACID of a transaction?

Why distributed transactions (concurrency control & atomic commit) ?

consensus (e.g. Raft) vs. distributed transactions (e.g. 2PC)

- [lecture notes](12.Distributed_Transactions.md)
- [material notes](6.033_Ch9.md)

## Lecture 13 - Spanner

Two-phase commit over Paxos - wide-area synchronous replication

Synchronized time (TrueTime API) for fast read-only transactions

**CASE STUDY: Spanner**

- [lecture notes](13.Spanner.md)
- [material notes](Spanner.md)

## Lecture 14 - Optimistic Concurrency Control

How does FaRM differ from Spanner? Both employ transactions + replication + sharding

What is RDMA? Why uses RDMA?

**CASE STUDY: FaRM**

- [lecture notes](14.Optimistic_Concurrency_Control.md)
- [material notes](FaRM.md)

## Lecture 15 - Big Data: Spark



**CASE STUDY: Spark**

- [lecture notes](15.Big_Data_Spark.md)
- [material notes](Spark.md)

## Lecture 16 - Cache Consistency: Memcached at Facebook

**CASE STUDY: Memcached**

- lecture notes
- material notes

## Lecture 17 - Cache Consistency: COPS

**CASE STUDY: COPS**

- lecture notes
- material notes

## Lecture 18 - Fork Consistency: Certificate Transparency

- lecture notes
- material notes

## Lecture 19 - Peer-to-peer: Bitcoin

**CASE STUDY: Bitcoin**

- lecture notes
- material notes

## Lecture 20 - Blockstack

**CASE STUDY: Blockstack**

- lecture notes
- material notes

## Lecture 21 - Project demos

- lecture notes
- material notes

## Project - rafter (inprogress)

[rafter](https://github.com/jasonyuchen/rafter) is a multi-group consensus library written in C++ inspired by [dragonboat](https://github.com/lni/dragonboat), [seastar](https://github.com/scylladb/seastar), [etcd](https://github.com/etcd-io/etcd), [braft](https://github.com/baidu/braft)
