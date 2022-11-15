# Streaming Systems

The What, Where, When, and How of large-scale data processing

*All animated figures are listed [here](http://streamingsystems.net/fig)*

## Part I. The Beam Model

1. **[Streaming 101](01.Streaming_101.md)**
   - basics of stream processing
   - common data processing patterns
2. **[The What, Where, When, and How of Data Processing](02.What_Where_When_How.md)**
   - detail and core concepts of robust stream processing
3. **[Watermarks](03.Watermarks.md)**
   - temporal progress metrics
   - 2 real-world implementation ()
4. **[Advanced Windowing](04.Advanced_Windowing.md)**
   - processing-time windows, sessions
   - continuation triggers

## Part II. Streams and Tables

5. **[Exactly-Once and Side Effects](05.Exactly_Once.md)**
   - end-to-end exactly-once (effectively-once)
   - 3 read-world implementation (Apache Flink, Apache Spark, Google Cloud Dataflow)
6. **[Streams and Tables](06.Streams_And_Tables.md)**
   - revisit MapReduce
   - the Beam Model (and beyond)
7. **[The Practicalities of Persistent State](07.The_Practicalities_Of_Persistent_State.md)**
   - persistent state in streaming pipelines
   - a general state management mechanism
8. **[Streaming SQL](08.Streaming_SQL.md)**
   - the meaning of streaming within the context of SQL
   - incorporate robust streaming semantics in SQL
9.  **[Streaming Joins](09.Streaming_Joins.md)**
    - various different join types within the context of streaming
    - temporal validity windows
10. **[The Evolution of Large-Scale Data Processing](10.The_Evolution_Of_Large_Scale_Data_Processing.md)**

## RisingWave

[RisingWave](https://www.risingwave-labs.com/) is an open-source cloud-native streaming database that uses SQL as the interface to manage and query data. It is designed to reduce the complexity and cost of building real-time applications. RisingWave consumes streaming data, performs incremental computations when new data come in, and updates results dynamically. As a database system, RisingWave maintains results in its own storage so that users can access data efficiently. You can sink data from RisingWave to an external stream for storage or additional processing.

Some interesting engineering topics from RisingWave team:

- [State Management for Cloud Native Streaming: Getting to the Core](https://www.risingwave-labs.com/blog/state-management-for-cloud-native-streaming/)
