# Coordinated Omission, CO

[original post](http://highscalability.com/blog/2015/10/5/your-load-generator-is-probably-lying-to-you-take-the-red-pi.html)

[explanation](https://www.scylladb.com/2021/04/22/on-coordinated-omission/)

## 系统类型 System Type

[open vs. close](https://www.usenix.org/legacy/event/nsdi06/tech/full_papers/schroeder/schroeder.pdf)

- **开放系统 Open Model**：所有请求独立的发生并施加到开放系统上，请求的产生不会受系统处理速度的影响
  
  ```cpp
  for (;;) {
    // various requests are being made independently
    std::thread([]() {
      make_request("a request");
    })
  };
  ```

- **封闭系统 Close Model**：在一批初始请求后，只有当请求被处理完成后才会发出新的请求，即类似闭环控制，请求的发起速度受系统处理速度约束

  ```cpp
  std::thread([]() {
    for (;;) {
      // only return when completed
      // the next request will be initiated afterwards
      make_request("a request");
    }
  });
  ```

## 负载生成 Load Generation

往往被测试系统的处理能力（server端）会超出测试系统（client端），因此合理的**负载生成方式需要能够覆盖被测试系统的处理能力**

1. 简单方式

    ```cpp
    for (auto i : range{1, X}) {
      std::thread([]() {
        make_request("a request");
      });
    }
    ```

   这种方式过于简单，主要缺点如下：
   - 创建线程的操作非常昂贵，开销在200us级别
   - 调度大量线程的操作也有开销，上下文切换开销在1-10us级别
   - 最大可创建线程数有额外的约束

2. 预分配资源

    ```cpp
    for (auto i : range{1, N}) {
      std::thread([]() {
        for (;;) {
          make_request("a request");
        }
      });
    }
    ```

  预分配N个线程，每个线程作为一个客户端并发发送请求，但是新的问题在于这样的客户端会**串行顺序发送请求**，则其发送请求的速度取决于服务端的处理速度（返回结果后才发起下一个请求）即变为了封闭系统测试 close model，同时存在**协调遗漏问题**

通常有两种方式进行请求发起的调度：

- **静态 static**：固定每隔一段时间发起请求

  ```text
  *: initiated, 
  time -----0---------1---------2-----> seconds
            |* * * * *|* * * * *|       Static uniform distributed requests
  ```

- **动态 dynamic**：下一次请求的发起在上一次请求完成后立即发起，但是不会快于一个最小延迟minimum time delay，通常采用**限流器rate limiter配合实现**

  ```text
  *: initiated
  -: not finished
  time -----0---------1---------2-----> seconds
            |*--* *-*---* *-* * |       Minimum delay
                 ^       ^   ^ ^
           minimum delay
  ```

当系统负载过大导致调度的请求不能按时完成从而影响后续请求时，也同样有两种处理方式：

- **队列 Queueing**：将未能按时发起的请求加入队列等待，并**在系统响应时尽快发出，这种方式配合静态调度是最为常见的**，广泛使用在YCSB，Cassandra-stress，wrk2等测试工具中，而配合**动态调度时往往需要一个全局的限流器**（配合minimum delay）限制峰值流量

  ```cpp
  // static with queueing
  for (auto i : range{1, N}) {
    std::thread([]() {
      for (size_t req = 1; req <= num; req++) {
        make_request("a request");
        wait_next(req + 1);
      }
    });
  }
  ```

  ```text
  time -----0---------1---------2-----> seconds
            |* * * * *|* * * * *|       Static uniform distributed requests
            |* *      |******* *|       1 slow request block others
  ```

- **丢弃 Queueless**：简单丢弃未能按时发出的请求

  ```text
  time -----0---------1---------2-----> seconds
            |* * * * *|* * * * *|       Static uniform distributed requests
            |* *      |  * * * *|       1 slow request leads to 4 dropped requests
  ```

## 协调遗漏问题 The Coordinated Omission Problem

Coordinated Omission问题主要描述了不良设计的观测系统会丢失一些关键的数据点，从而导致最后的延迟分布等数据失真，可能在如下场景中出现：

- **压力测试 Load Testing**
  通常压力测试的生成器会根据一定速率生成请求并访问被测系统，记录每个请求的响应时间，并每隔一定时间收集积累的响应时间生成延迟分布图，P99等数据

  这里有一个致命的问题，例如测试系统每隔1秒发出请求，但是某一次请求的延迟达到了1.5秒，则本应在1秒时发出的第二个请求延迟了0.5秒再发出（等同于排队时间0.5秒），此时再记录**收到响应与发出请求的时间差作为延迟数据，就丢失了排队延迟**

  ```text
  time -----0---------1---------2-----> seconds
            |* * * * *|* * * * *|       Uniform distributed requests
            |* *      |******* *|       1 slow request block others
            |* *      |*********XXXXXX  Retry may lead to AVALANCHE!
  ```

  因此只有当响应时间短于压力测试的请求发送间隔时，数据才没有失真，例如YCSB等常见的压力测试系统引入**意向时间intended time**，即意图发起请求的时间，此事件会早于或等于实际发出请求的时间，而**延迟就修改为收到响应与意向时间的时间差，此时就包含了等待时间**

- **监测系统 Monitoring Code**
  与压力测试同理，通常监测系统会在某次调用前后加上两个时间戳来记录时间差，此时假如某次调用耗时很长而阻塞了后面的调用，则**后面的调用都在排队等候，但这些事件也不会被监测系统的时间戳记录**

协调遗漏实际上使得测试延迟时间的设计变成了测试服务时间，而**真正的延迟时间是服务时间与等候时间之和**，因此在**请求产生的时刻就引入意向时间**来解决不真实的数据分布问题，最好的测试设计是**静态调度statci schedule、队列queueling以及延迟修正latency correction**
