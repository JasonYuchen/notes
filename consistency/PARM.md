# PRAM

[original post](https://jepsen.io/consistency/models/pram)

Pipeline Random Access Memory, **PRAM指由同一个进程顺序执行的写入操作应该以该顺序被全局所有进程观测到**，而对于不同进程的写入则可以以任意顺序被观测到

PRAM等同于**读己之写read your writes + 单调写monotonic writes + 单调读monotonic reads**

- **PRAM是一个单对象模型 single-object model**
- **只要客户端确保与固定的同一个服务端通信，则PRAM可以实现完全可用 totally available**
  `TODO: why`
