# Streaming Systems

The What, Where, When, and How of large-scale data processing

*All animated figures are listed [here](http://streamingsystems.net/fig)*

## Part I. The Beam Model

1. **[Streaming 101](01.Streaming_101.md)**
   - basics of stream processing
   - common data processing patterns
2. **[The What, Where, When, and How of Data Processing]**
   - detail and core concepts of robust stream processing
3. **[Watermarks]**
   - temporal progress metrics
   - 2 real-world implementation ()
4. **[Advanced Windowing]**
   - processing-time windows, sessions
   - continuation triggers

## Part II. Streams and Tables

5. **[Exactly-Once and Side Effects]**
   - end-to-end exactly-once (effectively-once)
   - 3 read-world implementation (Apache Flink, Apache Spark, Google Cloud Dataflow)
6. **[Streams and Tables]**
   - revisit MapReduce
   - the Beam Model (and beyond)
7. **[The Practicalities of Persistent State]**
   - persistent state in streaming pipelines
   - a general state management mechanism
8. **[Streaming SQL]**
   - the meaning of streaming within the context of SQL
   - incorporate robust streaming semantics in SQL
9.  **[Streaming Joins]**
    - various different join types within the context of streaming
    - temporal validity windows
10. **[The Evolution of Large-Scale Data Processing]**