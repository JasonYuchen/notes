# Read Uncommitted

[original post](https://jepsen.io/consistency/models/read-uncommitted)

**读未提交即允许读取到尚未提交事务的写入数据**，允许出现脏读 dirty read，但是不允许出现脏写 dirty write，即并发事务不能修改同一个数据

读未提交属于最为宽松的模型，几乎允许出现所有（除了脏写）行为

- **读未提交是一个事务模型 transactional model**
- **读未提交也是一个多对象属性 multi-object property**
- **读未提交可以实现完全可用 totally available**
  `TODO: why`

读未提交对时间没有任何要求，同时对同一进程内的不同事务也没有要求

读未提交禁止以下情况：

- **脏写 dirty write**：`w1(x)...w2(x)`

读未提交允许出现：

- **脏读 dirty read**：`w1(x)...r2(x)`
- **不可重复读 non-repeatable read/fuzzy read**：`r1(x)...w2(x)`
- **幻读 phantom**：`r1(P)...w2(y in P)`，`P`代表一个谓词，即事务1读取了谓词`P`满足的数据，而随后事务2写入了同样满足的新数据`y`，显然事务1并没有读到这个晚加入的新数据
