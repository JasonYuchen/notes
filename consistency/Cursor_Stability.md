# Cursor Stability

[original post](https://jepsen.io/consistency/models/cursor-stability)

游标稳定性在读已提交的基础上避免了丢失更新，其引入了游标的概念，游标指向一个**被事务读取**的对象，一旦一个对象被某个事务的游标指向，直到该事务释放游标前，相应对象都无法被人以其他事务修改（类似于锁），因此当事务1读取了对象后，事务2就无法在读取后再修改对象，因此事务2的修改也就不会丢失

- **游标稳定性是一个事务模型 transactional model**
- **游标稳定性也是一个多对象属性 multi-object property**
- **游标稳定性无法完全可用 totally available**
  `TODO: why cannot`

游标稳定性对时间没有任何要求，同时对同一进程内的不同事务也没有要求

游标稳定性禁止以下情况：

- **脏写 dirty write**：`w1(x)...w2(x)`
- **脏读 dirty read**：`w1(x)...r2(x)`
- **更新丢失 lost update**

游标稳定性允许出现：

- **不可重复读 non-repeatable read/fuzzy read**：`r1(x)...w2(x)`
- **幻读 phantom**：`r1(P)...w2(y in P)`，`P`代表一个谓词，即事务1读取了谓词`P`满足的数据，而随后事务2写入了同样满足的新数据`y`，显然事务1并没有读到这个晚加入的新数据
