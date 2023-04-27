# [SIGMOD 2019] DuckDB: an Embeddable Analytical Database

## Introduction

The following requirements for embedded analytical databases:

- High efficiency for **OLAP** workloads, but without completely sacrificing **OLTP** performance.
- High degree of **stability**
- **Efficient transfer** of tables to and from the database
- Practical embeddability and **portability**

## Design and Implementation

*While DuckDB is first in a new class of data management systems, **none of DuckDB’s components is revolutionary** in its own regard*

- **API**: C/C++/SQLite
- **SQL Parser**: `libpq_query`
- **Optimizer**: Cost-based
  - join order optimization based on **dynamic programming** with a **greedy fallback** for complex join graphs
  - **arbitrary subqueries flattening** based on *Unnesting Arbitrary Queries*
  - cardinality estimation: sampling + HyperLogLog
- **Execution Engine**: Vectorized
  - Vector Volcano model
- **Concurrency Control**: Serializable MVCC
  - **HyPer's serializable variant of MVCC**: update data in-place immediately, keeps previous tates stored in a separate undo buffer
- **Storage**: DataBlocks
  - horizontally **partitioned** in to chunks of columns
  - light-weight compression
  - **min/max indexes** carried with blocks
  - lightweight **index for every column** carried with blocks
