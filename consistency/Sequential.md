# Sequential Consistency

[original post](https://jepsen.io/consistency/models/sequential)

[采用光锥形象解释](Strong_consistency_models.md)

顺序一致性要求所有操作的实际发生顺序有一个**全序，并且每个进程都能观测到这个一致的顺序**，但是其**对于时间没有约束**（一个进程可以早于/晚于系统中的其他进程观测到这个全序，即**进程可以读取到过时的数据**，只要该进程观测到了某个状态，一定不会出现时间回溯观测到更旧的状态）

- **顺序一致性是一个单对象模型 single-object model**
- **顺序一致性无法实现完全可用 totally available**
  `TODO: why cannot`

若在顺序一致性的要求之上，要求满足实时性（例如希望通过**侧边信道side channel**通知其他进程一些事件的发生），则需要考虑[线性一致性](Linearizable.md)，而若希望能够实现完全可用，并且容忍因果无关的操作顺序没有全序（因果依赖不被打破）可以考虑[因果一致性 causal consistency](Causal.md)
