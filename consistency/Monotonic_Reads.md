# Monotonic Reads

[original post](https://jepsen.io/consistency/models/monotonic-reads)

单调读要求一个进程执行的连续读取操作，不可能发生时间回溯，**晚发生的读取不应该读取到旧的数据**

- **单调读是一个单对象模型 single-object model**
- **单调读可以实现完全可用 totally available**
  `TODO: why`
