# Monotonic Atomic View

[original post](https://jepsen.io/consistency/models/monotonic-atomic-view)

单调原子视图表达了ACID中的**原子性atomicity**，即事务的修改应该全都生效或全都不生效，一旦事务1中的某个写入能够被事务2所观测到，则事务1中的所有写入都应该可以被观测到

- **单调原子视图是一个事务模型 transactional model**
- **单调原子视图也是一个多对象属性 multi-object property**
- **单调原子视图可以实现完全可用 totally available**
  `TODO: why`

单调原子视图对时间没有任何要求，同时对同一进程内的不同事务也没有要求

单调原子视图禁止以下情况：

- **脏写 dirty write**：`w1(x)...w2(x)`
- **脏读 dirty read**：`w1(x)...r2(x)`

单调原子视图允许出现：

- **不可重复读 non-repeatable read/fuzzy read**：`r1(x)...w2(x)`
- **幻读 phantom**：`r1(P)...w2(y in P)`，`P`代表一个谓词，即事务1读取了谓词`P`满足的数据，而随后事务2写入了同样满足的新数据`y`，显然事务1并没有读到这个晚加入的新数据
