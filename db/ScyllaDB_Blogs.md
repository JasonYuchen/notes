# ScyllaDB Blogs

[ScyllaDB Engineering Resources](https://resources.scylladb.com/scylladb-engineering)

## CDC Source Connector

ScyllaDB CDC Source Connector is a source connector capturing row-level changes in the tables of a ScyllaDB cluster. It is a [Debezium](https://github.com/debezium/debezium) connector, compatible with Kafka Connect (with Kafka 2.6.0+). The connector reads the CDC log for specified tables and produces Kafka messages for each row-level `INSERT`, `UPDATE` or `DELETE` operation.

The connector is **fault-tolerant, retrying reading** data from Scylla in case of failure. It **periodically saves the current position** in the ScyllaDB CDC log using Kafka Connect offset tracking. Each generated Kafka message contains information about the source, such as the timestamp and the table name.

## Common Mistakes with ScyllaDB

[Infrastructure](https://www.scylladb.com/2023/04/27/top-mistakes-infrastructure/)

[Storage](https://www.scylladb.com/2023/07/17/top-mistakes-with-scylladb-storage/)

## Strongly Consistent Topology Change

Each read or write in ScyllaDB is now **signed with the current topology version** of the coordinator performing the read or write.

- If the replica has **newer**, incompatible topology information, it responds with an error, and the coordinator refreshes its ring and re-issues the query to new, correct replicas.
- If the replica has **older** information, which is incompatible with the coordinator’s version, it refreshes its topology state before serving the query.

This will make sure that drivers **never perform writes based on an outdated topology**, and, for schema changes, will make sure that a write into a newly created table never fails with “no such table” because the schema didn’t propagate yet.

[ScyllaDB’s Path to Strong Consistency: A New Milestone](https://www.scylladb.com/2023/05/04/scylladbs-path-to-strong-consistency-a-new-milestone/)

[What’s Next on ScyllaDB’s Path to Strong Consistency](https://www.scylladb.com/2023/05/09/whats-next-on-scylladbs-path-to-strong-consistency/)

## Tablets-based Replication

The Vnode-based replication strategy tries to **evenly distribute the global token space shared by all tables among nodes and shards**. It’s very simplistic.

- Vnodes (**token space split points**) are chosen randomly, which may cause an **imbalance** in the actual load on each node.
- For relatively small tables, whose token space is **fragmented** into many small chunks.

Strongly-consistent tables (**LWT, Paxos-based**)

- Slow
- 3 rounds to replicas per request
- Concurrent conflicting -> retries -> negative scaling
- No latency on leader failure
- Load easy to distribute

Strongly-consistent tables (**Raft**)

- Fast
- 1 round to relicas (on leader)
- Pipelining all the way down to each core
- No retries
- Extra latency on leader failure
- Extra 1 hop if not on the leader (drivers can be leader-aware)
- Need **Raft Groups** to distribute load, may need more tokens inside the replication metadata to have more ranges and more narrow ranges to distribute load, which may lead to **explosion of metadata and management overhead**

**Tablets** with Raft

- Per table replication metadata (not per keyspace)
- Adapative tablet partitioning, when tables **grow** and hit a threshold, or the tablet becomes **popular**, they will be split
- Every Raft group will be associated with exactly one tablet

[Why ScyllaDB is Moving to a New Replication Algorithm: Tablets](https://www.scylladb.com/2023/07/10/why-scylladb-is-moving-to-a-new-replication-algorithm-tablets/)
