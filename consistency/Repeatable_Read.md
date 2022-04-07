# Repeatable Read

[original post](https://jepsen.io/consistency/models/repeatable-read)

可重复读与可串行化非常接近，在可串行化的基础上**允许出现幻读phantom**的情况

- **可重复读是一个事务模型 transactional model**
- **可重复读无法完全可用 totally available**

可重复读对时间没有任何要求，同时对同一进程内的不同事务也没有要求（**单个事务内的读取必须可重复**）

可重复读禁止以下情况：

- **脏写 dirty write**：`w1(x)...w2(x)`
- **脏读 dirty read**：`w1(x)...r2(x)`
- **不可重复读 non-repeatable read/fuzzy read**：`r1(x)...w2(x)`

可重复读允许出现：

- **幻读 phantom**：`r1(P)...w2(y in P)`，`P`代表一个谓词，即事务1读取了谓词`P`满足的数据，而随后事务2写入了同样满足的新数据`y`，显然事务1并没有读到这个晚加入的新数据
