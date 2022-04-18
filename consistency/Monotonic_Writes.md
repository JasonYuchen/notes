# Monotonic Writes

[original post](https://jepsen.io/consistency/models/monotonic-writes)

单调写要求一个进程执行的连续写入操作，不可能发生时间回溯，**晚发生的写入不应该被早发生的写入覆盖**，所有进程都应该优先观测到一个进程上先发生的写入，再观测到晚发生的写入

- **单调写是一个单对象模型 single-object model**
- **单调写可以实现完全可用 totally available**
  `TODO: why`
