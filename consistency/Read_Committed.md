# Read Committed

[original post](https://jepsen.io/consistency/models/read-committed)

**读已提交即不会读取到尚未提交事务的写入数据**，不允许出现脏读 dirty read

- **读已提交是一个事务模型 transactional model**
- **读已提交也是一个多对象属性 multi-object property**
- **读已提交可以实现完全可用 totally available**
  `TODO: why`

读已提交对时间没有任何要求，同时对同一进程内的不同事务也没有要求

读已提交禁止以下情况：

- **脏写 dirty write**：`w1(x)...w2(x)`
- **脏读 dirty read**：`w1(x)...r2(x)`

读已提交允许出现：

- **不可重复读 non-repeatable read/fuzzy read**：`r1(x)...w2(x)`
- **幻读 phantom**：`r1(P)...w2(y in P)`，`P`代表一个谓词，即事务1读取了谓词`P`满足的数据，而随后事务2写入了同样满足的新数据`y`，显然事务1并没有读到这个晚加入的新数据
