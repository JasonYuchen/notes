# Writes Follow Reads

[original post](https://jepsen.io/consistency/models/writes-follow-reads)

读后写 writes follow reads也称为**会话因果一致性 session causality**，要求一个进程在完成读取后执行的写入，其可见顺序一定位于此次读取对应数据的写入操作之后，即`1.write() -> 2.read() -> 3.write()`第三步的写入一定晚于第一步的写入被观测到，或者说**一旦观测到了某些数据后再执行的操作不可能改变这个观测点之前的历史**

- **读后写是一个单对象模型 single-object model**
- **读后写可以实现完全可用 totally available**
  `TODO: why`
