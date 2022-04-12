# Linearizability

[original post](https://jepsen.io/consistency/models/linearizable)

[采用光锥形象解释](Strong_consistency_models.md)

线性一致性（也有一些文献称为外部一致性、强一致性、原子一致性）是最强的**单对象一致性模型**（相区分于串行化等多对象属性模型），其要求对一个对象的所有操作都表现出原子化并且与这些操作的实时顺序一致，[如此处](https://github.com/JasonYuchen/notes/blob/master/ddia/09.Consistency_and_Consensus.md#线性一致性-linearizability)

- **线性一致性是一个单对象模型 single-object model**
- **线性一致性无法实现完全可用 totally available**
  `TODO: why cannot`

若在线性一致性的要求之上，要求**对多个对象**实现线性一致性，则需要考虑[严格可串行化 strict serializability](Strict_Serializability.md)，而若对实时操作顺序没有要求**仅要求所有进程都能观测到一个相同一致的操作顺序**（与真实的顺序可以不一致）可以考虑[顺序一致性 sequential consistency](Sequential.md)
