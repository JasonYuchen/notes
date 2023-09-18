# Selected notes from muduo

## 线程安全的对象生命期管理

1. 对象的创建很简单

    - **构造期间不泄露`this`指针**
      - 不在ctor中注册callback
      - 不在ctor中把this传给其他线程
      - 即便在ctor最后一行也不可以做上述行为
    - 可以使用**二段式构造：ctor + initializer**，在异步情况下几乎是必须的，参考[RAII in Asynchronous Structure](https://glaubercosta-11125.medium.com/c-vs-rust-an-async-thread-per-core-story-28c4b43c410c)

2. 线程安全的Observer有多难：C++无法根据指针判断所指对象是否依然有效（Java中只要引用不是null，对象一定有效）
3. 原始指针有何不妥：指针a/b都指向一个对象，当线程通过a将对象销毁后，b就成了空悬指针
4. 神器`shared_ptr/weak_ptr`

    - `shared_ptr`控制对象的生命期
    - `weak_ptr`不控制对象的生命期
    - `shared_ptr/weak_ptr`的`count`是原子操作
    - `shared_ptr/weak_ptr`自身的线程安全与其他STL容器一致，并发读互斥写，同时需要考虑如果原`shared_ptr`所指对象正好在加锁的临界区内销毁了，可能会导致临界区意外过长

5. 应用到Observer上：Observable保存`weak_ptr<Observer>`，同时使用时用`shared_ptr<Observer> obj(it->lock())`来尝试提升，若已被释放则`erase(it)`
6. `shared_ptr`技术与陷阱

    - 意外延长对象的生命期：只要还有一个指针存在，即使不再使用，对象也会一直留存
    - 函数参数：拷贝需要修改引用计数，因此比拷贝原始指针的开销要高，多数情况下考虑`const T &`的方式传参以避免拷贝
    - 析构动作在创建时被捕获：由此
      - 虚析构不再是必须的
      - `shared_ptr<void>`可以持有任何对象，且能安全释放
      - 智能指针对象可以安全的跨越模块（DLL/SO）边界，不会造成A模块分配的内存在B模块里被释放这种错误，具体实现为在A模块获得的SP，其控制块也是在A里`new`的，同时会在控制块里绑定默认的`deleter`或自定义的`deleter`（作为控制块对象的成员）；假设在B模块里析构，析构时则自动调用控制块对象的虚函数进行销毁，后者继续调用对应的`deleter`，通过虚表的地址回到了A模块里进行析构
    - 析构动作可以定制，即`deleter`
    - 析构所在的线程：最后一个指向对象的SP离开作用域时，对象就在这个线程里开始析构，注意如果析构耗时较长，可以考虑单独开一个析构线程
    - 现成的RAII handle：避免循环引用：owner持有`SP<child>`，child持有`weak_ptr<owner>`

7. `shared_ptr`与`unique_ptr`

    - `unique_ptr<T, D>`：`deleter`是`unique_ptr`本身的一部分，可以**用incomplete type去构造`unique_ptr`**，同时`deleter`不可运行期修改，**析构时必须已经可以看到complete type**，数据结构更紧凑，运行效率更高
    - `shared_ptr<T>`：`deleter`是通过保存在控制块内，析构时用虚函数调用的，因此**必须用complete type去构造`shared_ptr`**，同时`deleter`是control block的一部分可以运行期修改，**析构时允许尚未看到complete type**，数据结构开销多，且有原子操作，运行效率略低

8. 对象池
    - 继承`enable_shared_from_this<T>`：使用T对象的`this`指针时，可以通过`shared_from_this()`来获得一个`SP<T>`
    - 弱回调，通过`weak_ptr<T>(shared_from_this())`来产生T对象的`this`指针，访问时如果对象还活着就访问，否则忽略，即弱回调

## 多线程服务器的适用场合与常用编程模型

1. 进程与线程

    - 考虑容错、扩容、负载均衡、暂时离线
    - **Actor Model (Message Passing Interface)**

2. 多线程服务器的常用编程模型
    - 每个线程循环，**one loop per thread**：这种模型下，每个IO线程有一个event loop，需要让哪个线程干活就把timer或IO channel注册到哪个线程，数据处理任务可以继续分摊到线程池中由计算线程来完成
    - 线程池：只有计算任务的线程可以用阻塞队列实现一个线程池

   推荐模式：**non-blocking IO + IO multiplexing + thread pool**

3. **进程间通信只用TCP**

    - 两个进程间TCP通信 ，如果一个崩溃了，操作系统关闭连接，另一个进程立刻就能获得通知，但是**应用层的心跳**也是必要的
    - TCP连接是可再生的，任何一个进程都能单独重启，重建连接之后继续工作
    - TCP是字节流的通信方式，需要选择合适的消息协议，如Google Protocal Buffers
    - **使用netstat来检测进程间的通信**方便且有效
      - `netstat -tpna | grep :port`可以立刻列出用到某服务的客户端地址
      - netstat可以打印Revc-Q/Send-Q，如果某个在持续增加，说明对应的另一侧处理不力

4. 多线程服务器的适用场合

    - 必须使用单线程的场合
      - 程序可能会`fork`，**多线程情况下尽可能避免使用`fork`（[A fork in the road](https://www.microsoft.com/en-us/research/publication/a-fork-in-the-road/)），`fork()`只克隆调用它的那个线程**，这就导致比如一些锁被其他线程持有，但是其他线程没被克隆，结果就是死锁，唯一安全的做法是`fork()`之后立即`exec()`新程序
      - 限制程序的CPU占用率

    - 多线程程序的场景
      - 多个CPU线程
      - 线程间共享数据
      - 非均质服务，即事件响应有优先级
      - 利用异步操作
      - 能scale up
      - 能有效划分每个线程的责任和功能，Actor

5. 线程的创建与销毁的守则

    - 程序库不应该在未提前告知的情况下创建自己的背景线程，否则可能导致不期望的过多线程，同时背景线程难以管理
    - 尽量以相同的方式创建线程，例如都用`std::thread`
    - 在进入`main()`前不要启动线程，C++保证进入`main()`前就完成全局对象的构造（单线程），但是**不同编译单元之间的对象构造顺序是不确定的**，因此如果启动线程访问到了未被初始化的全局对象则会出问题
    - 最好在初始化时就完成所有线程的创建，动态增减线程有额外不必要的开销
    - 线程的销毁
      - 自然死亡：从主函数退出，唯一的正常退出方式
      - 非正常死亡：抛出异常或线程触发segfault等方式
      - 自杀：调用`pthread_exit()`立刻退出
      - 他杀：其他线程调用`pthread_cancel()`强制终止，应避免使用`pthread_cancel()`
    - **`exit()`会析构全局对象和已经构造完的函数静态对象，这就有潜在的死锁可能**，比如已获得锁的全局对象调用了`exit()`，而`exit()`就去析构这个全局对象，却在析构函数中又要求获得锁，发生死锁，事实上，既然需要退出，也可以考虑采用`_exit()`或`kill`，**一般长时间运行的程序不需要追求安全退出**，只需要让程序进入拒绝服务的状态即可

6. 信号处理

    - 现代Linux做法是用`signalfd`把信号直接转换为文件描述符事件，从而根本上避免signal handler，并且将`signalfd`融入到IO管理中

## 多线程模型

| #   | model                         | MP  | MT  | Blocking IO | IO reuse | keep-alive | concurrency | multi-core | overhead | cross-request | serial | thread count | comment                                        |
| --- | ----------------------------- | --- | --- | ----------- | -------- | ---------- | ----------- | ---------- | -------- | ------------- | ------ | ------------ | ---------------------------------------------- |
| 0   | accept + read/write           | No  | No  | Yes         | No       | No         | No          | No         | Low      | No            | Yes    | Fixed        | once a request, serially                       |
| 1   | accept + fork                 | Yes | No  | Yes         | No       | Yes        | Low         | Yes        | High     | No            | Yes    | Dynamic      | new process per request                        |
| 2   | accept + new thread           | No  | Yes | Yes         | No       | Yes        | Medium      | Yes        | Medium   | Yes           | Yes    | Dynamic      | new thread per request                         |
| 3   | prefork                       | Yes | No  | Yes         | No       | Yes        | Low         | Yes        | High     | No            | Yes    | Dynamic      |                                                |
| 4   | prethreaded                   | No  | Yes | Yes         | No       | Yes        | Medium      | Yes        | Medium   | Yes           | Yes    | Dynamic      |                                                |
| 5   | reactor                       | No  | No  | No          | Yes      | Yes        | High        | No         | Low      | Yes           | Yes    | Fixed        | single threaded reactor                        |
| 6   | reactor + new thread per task | No  | Yes | No          | Yes      | Yes        | Medium      | Yes        | Medium   | Yes           | No     | Dynamic      | thread per request                             |
| 7   | reactor + worker threads      | No  | Yes | No          | Yes      | Yes        | Medium      | Yes        | Medium   | Yes           | Yes    | Dynamic      | worker thread per connection                   |
| 8   | reactor + thread pool         | No  | Yes | No          | Yes      | Yes        | High        | Yes        | Low      | Yes           | No     | Fixed        | IO-thread + worker threads pool                |
| 9   | reactors in threads           | No  | Yes | No          | Yes      | Yes        | High        | Yes        | Low      | Yes           | Yes    | Fixed        | one loop per thread (thread per core, Seastar) |
| 10  | reactors in processes         | Yes | No  | No          | Yes      | Yes        | High        | Yes        | Low      | No            | Yes    | Fixed        | one loop per process (Nginx)                   |
| 11  | reactors + thread pool        | No  | Yes | No          | Yes      | Yes        | High        | Yes        | Low      | Yes           | No     | Fixed        | IO threads pool + worker threads pool (Netty)  |

- 方案2：原始简单的处理方式，客户不多时完全可以应对
- 方案5：基本的单线程reactor方案，适合IO密集但CPU不密集的应用，较难发挥多核的威力，通过注册回调，将网络部分与业务逻辑分离，代码的扩展性和可维护性高
- 方案8：网络部分与业务逻辑分离，线程池充分发挥多核的威力，但计算是线程池异步完成的，延迟比IO线程直接计算要高
- 方案9：muduo的方案：一个main reactor专门负责accept连接，随后所有读写都放到sub-reactors中完成；seastar的方案：每个物理核心绑定一个线程，运行一个reactor，每个事件循环负责自身所有IO和计算
- 方案11：在方案8的基础上通过multi reactors进一步加强系统的IO处理能力，最为灵活

根据ZeroMQ手册给出的建议，**event loop / (1024Mbits/s)的配置**较为合理，即在千兆以太网上的程序只需要一个reactor即可，但若TCP连接之间有优先级，那么单个reactor可能会导致优先级反转，因此非同质连接要用multi reactors来处理

***推荐fully asychronously eventloop-per-core / thread-per-core***，或是在不可避免需要引入同步阻塞调用时采用***reactors + worker threads pool***

## 网络编程

1. **非阻塞模式下应用层缓冲是必须的**

    - 例如发送100kb数据，系统只接受了80kb，此时应用程序应该返回，同时output buffer中有余下的20kb数据，并且注册`POLLOUT`，在下次系统可以接收时再一并发送，如果output buffer已经清空就应该停止关注`POLLOUT`，防止出现busy loop
    - TCP处理无边界的字节流协议，当前字节数不足以反序列化成一条消息时，就需要暂存在input buffer中，同时从系统中读到input buffer需要一次性读完，避免不断触发`POLLIN`出现busy loop
    - 在fd总数较少，活跃fd比例高时，epoll不见得比poll高效，且水平触发`LT`模式相比边缘触发`ET`模式不容易出现漏处理事件的问题，读写也不必等待`EAGAIN`节省了系统调用次数

2. 内存缓冲与网络IO的开销考虑

    - 在一个不繁忙的系统上，64KB缓冲足以容纳1Gb的以太网在500us里收到的所有数据
    - 利用栈上准备一个65536 bytes的临时缓冲，然后使用`readv()`读取数据，`iovec`第一块指向应用层缓冲，另一块指向这个临时缓冲，那么**只有数据过多才会读入一部分到临时缓冲中**，随后再追加到应用层缓冲里，而栈空间临时缓冲随后销毁释放，灵活使用临时缓冲使得性能和易用性达到更好的平衡
    - 千兆以太网的应用数据吞吐速率大约是117 MB/S，而DDR3/DDR4的数据吞吐速率大约是20-40 GB/S，因此**内存数据拷贝的开销是远低于网络IO的**（muduo中给出的数据较早，新的数据参考10G-40G以太网1-4 GB/S以及DDR4/DDR5的50-80 GB/S）
    - 例如数独求解这种**计算密集型的服务，优化内存拷贝没有意义**，极少流量就足以让CPU饱和
    - 若服务还需要与数据库等其他设施交互，则瓶颈更有可能在于数据库等其他设施，而不在于网络库的内存拷贝开销

3. 定时器

    - 多级时间轮
    - 堆（优先队列）
    - 二叉树（红黑树）
    - `timerfd_create/gettime/settime`系统调用API

