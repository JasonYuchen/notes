# Read Your Writes

[original post](https://jepsen.io/consistency/models/read-your-writes)

读己之写 read your writes要求一个进程写入数据后立即进行读取能够读取到自己的写入数据，也是绝大多数现实系统最基本的保证之一，需要特别注意**读己之写只发生在一个进程上**，即不能通过自己写入后再利用侧边信道去访问其他进程来读取

- **读己之写是一个单对象模型 single-object model**
- **只要客户端确保与固定的同一个服务端通信，则读己之写可以实现完全可用 totally available**
  `TODO: why`
