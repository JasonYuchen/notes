# [SIGMOD 2006] Integrating Compression and Execution in Column-Oriented Database Systems

## Introduction

**压缩compression**是能够显著提高数据库系统性能的手段，主要原因在于：

- 数据压缩之后，存储在磁盘上的物理位置更相近，从而减少了**磁盘寻道时间 seek time**
- 压缩后数据占用空间更小，从而减少了**磁盘传输时间 transfer time**
- 尺寸变小后的数据更容易被数据库页缓存buffer pool manager缓存，**提高了缓存命中率**
- 压缩带来的额外CPU开销往往能够被节约的IO所补偿（原文2006年IO瓶颈更为明显，而现在2023年则有了[明显的变化](https://github.com/JasonYuchen/notes/blob/master/seastar/Introduction.md#shared-nothing-design)）

## Related Work

`TODO`

## C-Store Architecture

`TODO`

## Compression Schemes

- **Null Suppression**
  通常通过消去数据中大量的0，采用能够**描述0的位置和数量**的方法来实现压缩
- **Dictionary Encoding**
  最为广泛使用的压缩模式，通过将**频繁出现的重复值采用更短小的编码**来替换实现压缩，
- **Run-length Encoding**
  对于**连续相等的数值**，采用`(value, start_position, run_length)`来替换实现压缩，对排序过的列、或是值域较小重复值多的列效果最佳，而在行存系统中RLE通常用于一个大字符串中压缩字符
- **Bit-Vector Encoding**
  每一个值采用一个Bitmap（在行存系统中使用较多，也称为Bitmap indices，参考[04.OLAD Index]），对于值域较小重复较多的列效果较好
- **Heavyweight Compression Schemes**
  **Lempel-Ziv Encoding**，即著名的LZ系列压缩算法，不像Huffman编码（依赖整个数据集的数据频率），LZ算法在压缩数据时可以动态生成pattern table，将输入的数据分割成**不同长度不重叠的数据块并不断构造数据块字典**，随后当**遇到相同数据块时直接采用指针**指向先出现的相同的块以实现压缩

## Compressed Query Execution

### Query Executor Architecture

通过给C-Store拓展了两个类来支持各类压缩技术：

1. 压缩块 *CompressionBlock*
   其用于**代表一块压缩的数据，并且提供访问接口**如下，以RLE为例，其数据形式是`(value, start_pos, run_length)`，则`getSize()`就会返回`run_length`，而`getStartValue()`返回`value`，`getEndPosition()`返回`start_pos + run_length - 1`

   | Properties      | Iterator Access | Block Information |
   |:-               |:-               |:-                 |
   |`isOneValue()`   |`getNext()`      |`getSize()`        |
   |`isValueSorted()`|`asArray()`      |`getStartValue()`  |
   |`isPosContig()`  |                 |`getEndPosition()` |

2. 数据源 *DataSource*
   作为**查询计划和存储管理之间的接口类**，掌握了存储信息，包括压缩方式、每个列可用的索引等，可以从磁盘上读取压缩的数据页并转换成*CompressionBlock*供上层使用，并且一些谓词可以被下推到*DataSource*来执行优化

### Compression-Aware Optimizations

这种优化方式的代价就是代码的复杂度，例如对于N种不同的压缩方式可以为每一个算子都提供N种优化算子，当需要**考虑多个输入源的算子时（例如join）则直接需要提供N^2种优化算子**

降低代码复杂度的方式就是引入*CompressionBlock*抽象，由抽象层提供一些表达数据块属性的方法（不同底层压缩技术实际上会影响这些属性），从而不同的算子根据自身的特点，结合压缩块的属性，来决定如何访问底层数据，最坏的情况下就是循环调用`getNext()`来逐个读取，一些优化例子如下表

- One Value + Contiguous Positions
  - **Aggregation**: If both the group-by and aggregate input blocks are of this type, then the aggregate input block can be aggregated with one operation (e.g. if size was 8 and aggregation was sum, result is 8*value)
  - **Join**: Perform optimization shown in the second if statement in Figure 1 (works in general, not just for RLE).
- One Value + Non-Contiguous Positions
  - **Join**: Perform optimization shown in the third if statement in Figure 1 (works in general, not just for bit-vector compression)
- One Value
  - **Aggregation Group-By**: The position list of the value can be used to probe the data source for the aggregate column so that only values relevant to the group by clause are read in
- Sorted
  - **Max/Mix Aggregation**: Finding the maximum or minimum value in a sorted block is a single operation
  - **Join**: Finding a value within a block can be done via binary search

## Experimental Results

`SKIP`
