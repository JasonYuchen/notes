# [SIGMOD 2020] Beyond Analytics: the Evolution of Stream Processing Systems

## 1 Introduction

![01](images/stream01.png)

## 2 Review of Foundations

### 2.1 Languages and Semantics

> Till today, various communities are working on establishing a language for expressing computations which combine streams and relational tables, without a clear consensus.

可以参考[Streaming Systems](https://github.com/JasonYuchen/notes/tree/master/streamingsystems)中的[Streaming SQL](https://github.com/JasonYuchen/notes/blob/master/streamingsystems/08.Streaming_SQL.md#chapter-8-streaming-sql)

TODO

### 2.2 Time and Order

由于网络波动、传输乱序等等，流数据通常不一定按照顺序抵达处理系统，此时就引入了乱序处理、重排序等过程，可以参考Streaming System中关于[处理时间的描述](https://github.com/JasonYuchen/notes/blob/master/streamingsystems/02.What_Where_When_How.md#chapter-2-the-what-where-when-and-how-of-data-processing)以及更深入的[Advanced Windowing](https://github.com/JasonYuchen/notes/blob/master/streamingsystems/04.Advanced_Windowing.md#chapter-4-advanced-windowing)

TODO

### 2.3 Tracking Processing Progress

追踪流处理系统的处理进度：

- **punctuation**
- **watermarks**: [Watermark](https://github.com/JasonYuchen/notes/blob/master/streamingsystems/03.Watermarks.md#chapter-3-watermarks)
- **heartbeats**
- **slack**
- **frontiers**

TODO

## 3 Evolution of System Aspects

### 3.1 State Management

[流处理的状态存储在设计时有哪些考虑](https://zhuanlan.zhihu.com/p/506869449)

[什么是流计算的状态](https://www.zhihu.com/question/62221304/answer/2312737176)

[Rethinking State Management in Cloud-native Streaming Systems]()

TODO

### 3.2 Fault Recovery and High Availability

TODO

### 3.3 Elasticity and Load Management

TODO

## 4 Prospects

### 4.1 Emerging Applications

- **Cloud Applications**
- **Maching Learning**
- **Streaming Graphs**

### 4.2 The Road Ahead

- **Programming Models**
- **Transactions**
- **Advanced State Backends**
- **Loops & Cycles**
- **Elasticity & Reconfiguration**
- **Dynamic Topologies**
- **Shared Mutable State**
- **Queryable State**
- **State Versioning**
- **Hardware Acceleration**
