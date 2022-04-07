# Strict Serializability

[original post](https://jepsen.io/consistency/models/strict-serializable)

严格串行化（也称为PL-SS、String 1SR、Strong 1SR）意味着所有操作的发生顺序都与**真实时间顺序real-time ordering**完全一致（假定存在一个完美同步的全局时钟）

- **严格串行化是一个事务模型 transactional model**
  每个事务中可以包括多个按顺序执行的**基本操作primitive operations**，而严格串行化保证这些事务都是原子发生的，即一个事务的基本操作不会与另一个事务的基本操作交织执行
- **严格串行化也是一个多对象属性 multi-object property**
  事务可以涉及到多个对象，而严格串行化可以认为对整个系统作为单个事务时其所有操作同样有效
- **严格串行化无法完全可用 totally available**
  出现网络分区时，部分或所有存活节点无法正常运转make progress

在实践中由于网络存在分区的可能性，一些节点无法正常工作，因此严格串行化是无法完全达到的，严格串行化同时隐含了[**串行化 serializability**和**线性一致性 linearizability**](https://github.com/JasonYuchen/notes/blob/master/ddia/09.Consistency_and_Consensus.md#1-什么使得系统保证线性一致性-what-makes-a-system-linearizable)，可以认为**严格串行化就是多对象上事务操作的全序再加上线性一致性的时间约束**，即同时保证多对象上的结果一致（串行化执行）和过程中的对象读写新鲜度（时间约束）

> strict serializability as **serializability's total order of transactional multi-object operations**, plus **linearizability's real-time constrains**
