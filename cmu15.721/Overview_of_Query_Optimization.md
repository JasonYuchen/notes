# [PODS 1998] An Overview of Query Optimization in Relational Systems

## Introduction

查询的执行引擎实现了一组物理算子physical operators，，而一次查询的**执行就可以抽象为由物理算子组成的查询算子树physical operator tree**，例如：

```text
              Index Nested Loop
                (A.x = C.x)
                  /     \
                 /       \
                /         \
        Merge-Join     Index Scan
        (A.x = B.x)        (C)
           /  \
          /    \
         /      \
      Sort      Sort
       |          |
       |          |
       |          |
  Table Scan  Table Scan
     (A)         (B)
```

查询优化器就负责根据输入的SQL查询，在可能的执行计划**space of possible execution plans**中来选择一个"最优"的，执行计划的搜索空间可能非常大：

- 代数表示的查询可以被转换为**逻辑等价**的另一个代数表达式，例如`JOIN(JOIN(A, B), C) -> JOIN(JOIN(B, C), A)`
- 对于给定的代数表达式，可以对应多种不同的物理算子，例如join可以是hash join、merge join等

因此对于优化器来说，其核心在于：

- **足够大的搜索空间（即包含成本低的计划） search space**
- **准确的成本估计 cost estimation**: 能够估计每个计划的成本，从而可以选择出成本最低的计划，通常其准确性对实际性能影响非常大
- **高效的枚举算法 enumeration algorithm**: 能够根据代数转换等方式枚举出不同的执行计划用于成本估计和选择

## An Example: System-R Optimizer

参考Qeury Planning [I](../cmu15.445/14.Query_Planning_I.md)/[II](../cmu15.445/15.Query_Planning_II.md)

## Search Space

### Commuting Between Operators

- **Generalizing Join Sequencing**
  Join往往满足**交换律commutative**和**结合律associative**，从而多表连接时往往可以调整Join顺序产生多种执行计划，但大部分系统只会考虑固定的Join顺序从而限制搜索空间的大小，例如System-R等系统只考虑**linear join**，即`JOIN(JOIN(JOIN(A, B), C), D)`这种形式；而**bushy join**，即`JOIN(JOIN(A, B), JOIN(C, D))`这种形式，需要构建临时表存储中间结果，但有些场景下性能会比linear join更佳
- **Outerjoin and Join**
  外连接（例如Left Outer Join LOJ）是非对称的，从而不满足交换律，但是依然可以考虑结合律，例如`JOIN(R, S LOJ T) -> JOIN(R,S) LOJ T`
- **Group-By and Join**
  通常SPJ查询伴随GroupBy时，SPJ部分往往会在GroupBy之前完成，而这也是可以通过转换来调整的，尤其是提前执行GroupBy往往可以极大的减少后续tuple的数量

### Reducing Multi-Block Queries to Single-Block

- **Merging Views**
  `Q = JOIN(R, V), V = JOIN(S, T) -> Q = JOIN(R, JOIN(S, T))`，但是当Views包含复杂的逻辑、GroupBy等非SPJ算子时，就需要应用其他转换（例如前述的提前GroupBy），随后才能考虑合并优化
- **Merging Nested Subqueries**
  例如在`WHERE`位置存在子查询，而逐条迭代则意味着每一条数据都要重新求值一次子查询来判断谓词是否满足，尝试[合并/展开子查询](14.Logic_Execution.md#sub-queries)就可以优化这种场景，例如以下转换：

  ```SQL
  SELECT Emp.Name
    FROM Emp.Dept IN
      SELECT Dept.Dept
        From Dept
          WHERE Dept.Loc='Denver' AND Emp.Emp = Dept.Mgr

  /* rewrite */

  SELECT E.Name
    FROM Emp AS E, Dept AS D
      WHERE E.Dept = D.Dept AND D.Loc='Denver' AND E.Emp = D.Mgr
  ```

### Using Semi-join Like Techniques for Optimizing Multi-Block Queries

`TODO`

## Statistics and Cost Estimation

> Optimization is only **as good as** its cost esitmates.

### Statistical Summaries of Data

- **Statistical Information on Base Data**
  对表的基本统计信息，将会直接影响到scan、join等算子的代价估计、内存占用等，例如对column数据的分布可以用于估计selectivity
- **Estimating Statistics on Base Data**
  对于海量数据可以采用**采样sampling**等方式来权衡精准性和效率
- **Propagation of Statistical Information**
  对于表的基本统计信息需要能够随着算子向上传播，从而让上层算子也能尽可能准确的估计代价，但显然每次传播都会发生一定程度的失真

### Cost Computation

成本估计往往会综合考虑CPU、磁盘I/O、分布式系统的网络I\O等

## Enumeration Architectures

枚举算法必须能够处理由加入了新的执行计划转换而带来的额外搜索空间，例如引入了新的join物理算子从而优化器必须考虑当前可以派生出的新物理执行计划的代价

**可扩展优化器extensible optimizer**的核心在于：

- Use of **generalized cost functions and physical properties** with operator nodes
- Use of a **rule engine** that allows transformations to modify the query expression or the operator trees
- Many exposed "knobs" that can be used to **tune the behavior of the system**

## Beyond the Fundamentals

- **Distributed and Parallel Databases**
  分布式/并行数据库系统还需要考虑到节点通信的网络开销、数据的储存位置等等因素
- **User-Defined Functions**
  参考[Froid](Froid.md#background)中对传统UDF计算过程缺点的描述
- **Materialized Views**
