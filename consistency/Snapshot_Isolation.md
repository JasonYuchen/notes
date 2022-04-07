# Snapshot Isolation

[original post](https://jepsen.io/consistency/models/snapshot-isolation)

快照隔离下，一个事务仿佛在操作整个数据库的一份快照，做出的所有修改仅对自己可见，并且在提交commit的瞬间原子性的将所有修改都对外可见，若两个事务分别读写了一个对象，则在其中一个事务提交成功后，另一个事务必须终止

- **快照隔离是一个事务模型 transactional model**
- **严格串行化也是一个多对象属性 multi-object property**
- **严格串行化无法完全可用 totally available**

与串行化要求所有事务的全序不同，**快照隔离仅要求部分有序partial order**，一个事务内的多个操作可以与其他事务的操作交织执行，快照隔离最显著的问题就是允许：

- **写偏差 write skews**：两个事务分别**读取存在重叠的对象，但是修改了不重叠的数据，并且分别都没有打破约束，而一起成功提交后就会打破约束**，例如约束要求`A+B>0`，事务一首先读取了`A=70`以及`B=80`并且将`A -= 80`此时依然未打破约束，而事务二首先读取了`A=70`以及`B=80`并且将`B -= 90`此时也未打破约束，两个事务均成功提交后就打破了约束
- **只读异常 read-only anomaly**：`R2(X0, 0) R2(Y0, 0) R1(Y0, 0) W1(Y1, 20) C1 [R3(X0, 0) R3(Y1, 20) C3] W2(X2, -11) C2`，只读的事务读取到了不应该出现的数值，**读取到了中间状态**，这是有一个事务已经提交而另一个事务尚未提交的状态，[事例出处](https://www.cs.umb.edu/~poneil/ROAnom.pdf)

快照隔离对时间没有任何要求，同时对同一进程内的不同事务也没有要求，对于快照隔离的基本实现原理可以[见此](https://github.com/JasonYuchen/notes/blob/master/ddia/07.Transactions.md#弱的隔离级别-weak-isolation-levels)

快照隔离应该满足以下四种属性：

- **内部一致性 internal consistency**：同一个事务内的读取总是能观测到最近的写入的结果
- **外部一致性 external consistency**：事务的读取一定能观测到前序已提交事务的写入
- **前缀 prefix**：事务提交后对所有节点的可见顺序应该是一致的
- **无冲突 no-conflict**：当事务写入相同的对象时，一个事务必须能观测到另一个事务的写入（即不允许并发写入/更新丢失）
