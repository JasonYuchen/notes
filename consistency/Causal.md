# Causal Consistency

[original post](https://jepsen.io/consistency/models/causal)

相比于顺序一致性要求所有操作构成全序，因果一致性仅要求**存在因果依赖关系的操作达成全序**并且所有进程都能观测到该一致的顺序，而对于不存在因果关系的操作则可以观测到任意的顺序

- **因果一致性是一个单对象模型 single-object model**
- **只要客户端确保与固定的同一个服务端通信，则因果一致性可以实现完全可用 totally available**
  `TODO: why`

比因果一致性稍强的**实时因果一致性模型 real-time causal**是被证明能够实现完全可用totally available的最强一致性模型，例如[causal+](https://github.com/JasonYuchen/notes/tree/master/mit6.824#lecture-17---causal-consistency-cops)，但由于因果一致性要求客户端只与固定的服务端通信，这在现实世界中往往是不可能的，并且需要不断维护保持因果依赖关系也引入了额外的代价
