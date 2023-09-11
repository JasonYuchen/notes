# Comprehensive Tutorial

[original](https://github.com/scylladb/seastar/blob/master/doc/tutorial.md)

## 简介 Introduction

[简介](Introduction.md)

## 初步 Getting started

[编译与测试](Setup.md)

## 线程和内存 Threads and memory

1. **线程 Seastar threads**
   在程序中使用`seastar::smp::count`来获知运行的线程数，并且在命令行中使用`-c<num>`来传入希望使用的线程数`<num>`，例如在双核四线程的机器上可以使用`-c4`，而当指定的线程数超过CPU虚拟内核数量时会报错
2. **内存 Seastar memory**
   seastar根据运行的线程数预先均分可用的内存量，并且在该线程shard内的分配（`malloc()`或`new`）都会只使用这块内存，可以在命令行使用`--reserve-memory`指定保留给OS的内存数量，或使用`-m<num><unit>`来指定给seastar使用的内存数量，单位`<unit>`可以是`k/M/G/T`，例如`-m10T`意味着允许seastar使用10T内存，超过实际物理内存量就会报错

## Inroducting futures and continuations

`TODO`

当计算完成时可以返回`seastar::make_ready_future<T>(...)`，此时对应的`.then()`就会被优化，直接调用而不是等到下一次循环，但是为了避免连续的`.then()`导致其他任务、事件循环饥饿starvation，会在连续运行256（默认值，无法通过配置修改）个任务时被抢占，后续任务会留到下一次事件循环返回时执行

## 协程 Coroutines

[协程](Coroutines.md)

## 连续 Continuations

`TODO`

## 处理异常 Handling exceptions

`TODO`

协程中的异常处理[见此](Coroutines.md#协程中的异常处理)

## 生命期管理 Lifetime management

由于异步编程中的对象可能在函数返回以后的某一刻才被使用到，所以其生命期管理非常重要

### 传递所有权 Passing ownership to continuation

最简单的方式就是将对象所有权传递给等待执行的异步任务，从而任意时刻异步任务需要执行时对象一定有效，并且当任务执行结束、或是因异常被跳过执行都会及时释放

```cpp
seastar::future<> slow_op(std::vector<int> v) {
    // v is not copied again, but instead moved:
    return seastar::sleep(10ms).then([v = std::move(v)] { /* do something with v */ });
}
```

但这种场合适用范围有限，因为经常会遇到异步任务只需要某数据结构的一部分数据这种情况，若是`std::move`之后整个数据都只有某个异步任务持有，或是遇到调用者还需要保存数据的引用以备后续使用的场合这种方式也不适用

### 保持所有权 Keeping ownership at the caller

当以引用的方式传入对象给异步编程任务时，调用者必须确保直到任务结束，传入的引用始终有效，seastar提供了`do_with()`方法来确保传入的对象生命周期与任务一样，`do_with()`接受任意多的右值对象，并且后面的任务必须接受引用参数：

```cpp
seastar::future<> f() {
    T obj; // wrong! will be destroyed too soon!
    return slow_op(obj);
}

seastar::future<> f() {
    return seastar::do_with(T(), [] (T obj) { // WRONG: should be T&, not T
        return slow_op(obj);
    }
}

seastar::future<> slow_op(T obj); // WRONG: should be T&, not T
seastar::future<> f() {
    return seastar::do_with(T(), [] (auto& obj) {
        return slow_op(obj);
    }
}

seastar::future<> f() {
    return seastar::do_with(T1(), T2(), [] (auto& obj1, auto& obj2) {
        return slow_op(obj1, obj2);
    }
}
```

需要特别注意的是，采用`do_with()`时传入对象的生命周期与`do_with()`返回的`future`一样长，一旦该`future`执行结束获得结果时，不应该有任何任务再指涉相应的对象，否则就是use-after-free

### 共享所有权 Sharing ownership (reference counting)

seastar提倡使用`std::unique_ptr`来代表独占所有权，使用`seastar::shared_ptr`来代表共享所有权，其与`std::shared_ptr`的差别在于**前者是单线程引用计数**而后者采用原子变量来适配多线程的场景，因此前者**不能被跨线程/shards使用**

seastar还额外提供了更加轻量的`seastar::lw_shared_ptr`，通过不支持多态的方式可以减少占用的空间（只需要一个引用计数变量即可）

### 栈上对象 Saving objects on the stack

seastar提供了`seastar::thread`（有栈空间）以及`seastar::async`来允许类似同步的代码写法：

```cpp
seastar::future<> slow_incr(int i) {
    return seastar::async([i] {
        seastar::sleep(10ms).get();
        // We get here after the 10ms of wait, i is still available.
        return i + 1;
    });
}
```

## 高级特性 Advanced futures

`TODO`

## 纤程 Fibers

seastar中的continuation通常非常简短，并通过`.then()`串联起非常多个continuations，这一连串的任务就称为一个fiber

## 循环 Loops

虽然通常尾递归tail call optimization可以被优化为迭代，且递归在很多时候能够更简洁的解决问题，但是递归本身也更容易出现误用并且有时候并不会被优化，因此更推荐使用采用下列函数的迭代算法

> why recursion is a possible, but bad, replacement for `repeat()`, see [comment](https://groups.google.com/g/seastar-dev/c/CUkLVBwva3Y/m/3DKGw-9aAQAJ)

### `repeat()`

采用`repeat()`的方式来循环执行一段continuation直到收到`stop_iteration`对象，并通过该对象来判断是否继续循环执行：

```cpp
seastar::future<int> recompute_number(int number);

seastar::future<> push_until_100(seastar::lw_shared_ptr<std::vector<int>> queue, int element) {
    return seastar::repeat([queue, element] {
        if (queue->size() == 100) {
            return make_ready_future<stop_iteration>(stop_iteration::yes);
        }
        return recompute_number(element).then([queue] (int new_element) {
            queue->push_back(new_element);
            return stop_iteration::no;
        });
    });
}
```

### `do_until()`

`do_until()`与`repeat()`类似，但是`do_until()`需要显式传入判断循环终止的条件

```cpp
seastar::future<int> recompute_number(int number);

seastar::future<> push_until_100(seastar::lw_shared_ptr<std::vector<int>> queue, int element) {
    return seastar::do_until([queue] { return queue->size() == 100; }, [queue, element] {
        return recompute_number(element).then([queue] (int new_element) {
            queue->push_back(new_element);
        });
    });
}
```

### `do_for_each()`

`do_for_each()`相当于常规的`for`循环，可以通过一个范围或是一对迭代器来指定运行范围，并且总是在前一个元素执行完毕才会开始执行后一个元素，循环的顺序性是保证的

```cpp
seastar::future<> append(seastar::lw_shared_ptr<std::vector<int>> queue1, seastar::lw_shared_ptr<std::vector<int>> queue2) {
    return seastar::do_for_each(queue2, [queue1] (int element) {
        queue1->push_back(element);
    });
}

seastar::future<> append_iota(seastar::lw_shared_ptr<std::vector<int>> queue1, int n) {
    return seastar::do_for_each(boost::make_counting_iterator<size_t>(0), boost::make_counting_iterator<size_t>(n), [queue1] (int element) {
        queue1->push_back(element);
    });
}

seastar::future<> do_for_all(std::vector<int> numbers) {
    // Note that the "numbers" vector will be destroyed as soon as this function
    // returns, so we use do_with to guarantee it lives during the whole loop execution:
    return seastar::do_with(std::move(numbers), [] (std::vector<int>& numbers) {
        return seastar::do_for_each(numbers, [] (int number) {
            return do_something(number);
        });
    });
}
```

### `parallel_for_each()`

与`do_for_each()`相对应的，`parallel_for_each()`是并发执行，范围内每个元素对应任务被一次性加入到reactor引擎的执行队列当中，因此不对执行顺序有任何保证

### `max_concurrent_for_each()`

有时候所有任务都添加进队列会引起过高的并发性反而导致性能劣化等问题，因此可以通过`max_concurrent_for_each()`并传入期望最高的并发度数量来限制并发度

```cpp
seastar::future<> flush_all_files(seastar::lw_shared_ptr<std::vector<seastar::file>> files, size_t max_concurrent) {
    return seastar::max_concurrent_for_each(files, max_concurrent, [] (seastar::file f) {
        return f.flush();
    });
}
```

## 等待多个事件 `when_all`：Waiting for multiple futures

采用`when_all()`可以发起并等待多个异步任务（必须是**右值**），并且这**一系列任务的结果是一个tuple会作为`when_all()`的返回值**，当不需要结果时可以使用`.discard_result()`，或是在下一个任务中使用这个tuple，如下：

```cpp
future<> f() {
    using namespace std::chrono_literals;
    future<int> slow_two = sleep(2s).then([] { return 2; });
    return when_all(
            sleep(1s),
            std::move(slow_two),
            make_ready_future<double>(3.5)
        ).then([] (auto tup) {
            std::cout << std::get<0>(tup).available() << "\n";
            std::cout << std::get<1>(tup).get0() << "\n";
            std::cout << std::get<2>(tup).get0() << "\n";
        });
}
```

当任务有可能抛出异常时，`when_all()`依然会**等到所有子任务执行结束**，异常依然会被包含在tuple中返回（若不通过`.ignore_ready_future()`忽略带有异常的结果，就会记录一条异常被忽略的日志）：

```cpp
future<> f() {
    using namespace std::chrono_literals;
    future<> slow_success = sleep(1s);
    future<> slow_exception = sleep(2s).then([] { throw 1; });
    return when_all(
            std::move(slow_success),
            std::move(slow_exception)
        ).then([] (auto tup) {
            std::cout << std::get<0>(tup).available() << "\n";  // available but succeeded
            std::cout << std::get<1>(tup).failed() << "\n";     // available but failed
            std::get<1>(tup).ignore_ready_future();
        });
}
```

seastar提供了更易于使用的`when_all_succeed()`，当**所有子任务都成功时，直接将结果提供给后续的任务**而不需要再逐个判断是否成功：

```cpp
future<> f() {
    using namespace std::chrono_literals;
    return when_all_succeed(
            sleep(1s),
            make_ready_future<int>(2),
            make_ready_future<double>(3.5)
        ).then([] (int i, double d) {  // sleep does not return a value
            std::cout << i << " " << d << "\n";
        });
}
```

对于`when_all_succeed()`而言，需要使用`handle_exception()`来处理可能抛出的异常：

```cpp
future<> f() {
    using namespace std::chrono_literals;
    return when_all_succeed(
            make_ready_future<int>(2),
            make_exception_future<double>("oops")
        ).then([] (int i, double d) {
            std::cout << i << " " << d << "\n";
        }).handle_exception([] (std::exception_ptr e) {
            std::cout << "exception: " << e << "\n";
        });
}
```

除了可变模板参数的`when_all/when_all_succeed`以外，还可以使用一对迭代器来指定一个任务范围（例如传入`std::vector<future<T>>`的`begin()/end()`）

## 信号量 Semaphores

### 采用信号量来限制并发度 Limiting parallelism with semaphores

采用`thread_local`的方式使得`g()`内部限制最大并发执行量，并且semaphore也是每个shard相互独立的

```cpp
seastar::future<> g() {
    static thread_local seastar::semaphore limit(100);
    return limit.wait(1).then([] {
        return slow(); // do the real work of g()
    }).finally([] {
        limit.signal(1);
    });
}
```

在上述代码中，**异常安全非常重要**：

- `.wait(1)`有可能抛出异常（内存耗尽，semaphore需要维护一个等待链表），此时计数器不会减少，同时`.finally()`也不会执行则计数器也不会增加
- `.wait(1)`有可能返回一个异常future（例如semaphore已经失效，注意与抛出异常相区别），此时对异常future后续调用`.signal(1)`也会没有任何作用
- `slow()`也有可能抛出异常或返回异常状态，而这不用担心因为`.finally()`一定会执行并维护计数器的值

显然当执行的任务更多，逻辑越来越复杂的情况下合理的维护semaphore也愈加困难，而在**C++中采用RAII语义**能更简洁的确保异常安全，**seastar提供了`seastar::with_semaphore()`来确保异常情况下计数器的值都被正确维护**：

```cpp
seastar::future<> g() {
    static thread_local seastar::semaphore limit(100);
    return seastar::with_semaphore(limit, 1, [] {
        return slow(); // do the real work of g()
    });
}
```

另外也可以使用`seastar::get_unites()`返回特殊的unit对象来确保计数器正常：

```cpp
seastar::future<> g() {
    static thread_local semaphore limit(100);
    return seastar::get_units(limit, 1).then([] (auto units) {
        return slow().finally([units = std::move(units)] {});
    });
}
```

### 限制资源使用 Limiting resource use

semaphore支持传入任意数值，因此也可以用来限制诸如内存等资源的使用：

```cpp
seastar::future<> using_lots_of_memory(size_t bytes) {
    static thread_local seastar::semaphore limit(1000000); // limit to 1MB
    return seastar::with_semaphore(limit, bytes, [bytes] {
        // do something allocating 'bytes' bytes of memory
    });
}
```

### 限制循环并发数 Limiting parallelism of loops

同时执行的循环的并发数也可以通过semaphore来限制，一个简单的循环如下没有任何并发，下一个`slow()`每次都在前一个`slow()`结束时才会开始：

```cpp
seastar::future<> slow() {
    std::cerr << ".";
    return seastar::sleep(std::chrono::seconds(1));
}
seastar::future<> f() {
    return seastar::repeat([] {
        return slow().then([] { return seastar::stop_iteration::no; });
    });
}
```

假设对`slow()`的顺序没有要求，且希望**同时执行多个`slow()`，则可以不关注`slow()`的返回**并每次都直接开始下一个`slow()`如下，此时不等待`slow()`的返回而是一瞬间就开始执行大量的`slow()`，并发数没有任何限制：

```cpp
seastar::future<> f() {
    return seastar::repeat([] {
        slow();
        return seastar::stop_iteration::no;
    });
}
```

采用semaphore可以限制当前正在执行的`slow()`个数：

- 在这里无法使用单个`thread_local`的semaphore，因为`f()`可以调用多次且每次调用都需要独立限制底层并发执行`slow()`的数量，因此采用`do_with()`
- 调用`seastar::futurize_invoke(slow)`的返回值并没有被返回，即不等待`slow()`的结果，只要semaphore有额度就直接开始下一个`slow()`
- 在这里无法使用`with_semaphore()`，因为该函数必须等到lambda结束时才能操作semaphore的计数器，从而`slow()`必须顺序执行而不能并发
- 在这个场景下也可以使用`get_units()`

```cpp
seastar::future<> f() {
    return seastar::do_with(seastar::semaphore(100), [] (auto& limit) {
        return seastar::repeat([&limit] {
            return limit.wait(1).then([&limit] {
                seastar::futurize_invoke(slow).finally([&limit] {
                    limit.signal(1); 
                });
                return seastar::stop_iteration::no;
            });
        });
    });
}

seastar::future<> f() {
    return seastar::do_with(seastar::semaphore(100), [] (auto& limit) {
        return seastar::repeat([&limit] {
            return seastar::get_units(limit, 1).then([] (auto units) {
                slow().finally([units = std::move(units)] {}); // must move the unit to the final lambda i.e. finally
                return seastar::stop_iteration::no;
            });
        });
    });
}
```

上述例子主要展示多个循环执行相同的任务如何将相同的任务并发执行，而没有限制执行次数，即死循环，通常实践中会有最终循环次数限制，即例如要求单次同时执行不超过100个，一共执行456次：

- 按顺序从0-456开始执行，前100个`slow()`能立即开始，后续每有一个`slow()`结束就能开启一个新的`slow()`
- 最终执行完456个，期间semaphore一直接近0，每有释放的计数就立即开始新任务，最终`limit.wait(100)`等待计数器返回100时所有任务执行结束（中途不可能回到100）
- 若没有`finally()`来等待结束，则`f()`返回的future会在发起最后100个`slow()`后就立即ready，而不会等这100个`slow()`完成

```cpp
seastar::future<> f() {
    return seastar::do_with(seastar::semaphore(100), [] (auto& limit) {
        return seastar::do_for_each(
                boost::counting_iterator<int>(0),
                boost::counting_iterator<int>(456),
                [&limit] (int i) {
                    return seastar::get_units(limit, 1).then([] (auto units) {
                        slow().finally([units = std::move(units)] {});
                    });
                }).finally([&limit] { return limit.wait(100); });
    });
}
```

采用semaphore同时用于一个限制循环任务的并发数和等待任务的全部完成，当需要采用semaphore限制多个循环任务的并发数时，就不能再用于等待全部循环任务的结束，而需要采用`seastar::gate`：

- 此时semaphore是`thread_local`来限制所有在运行的循环`f()`中的`slow()`并发执行数
- 每个`slow()`在执行前后需要`gate.enter()/gate.leave()`（也可以采用RAII的方式`seastar::with_gate()`），并且最后通过`gate.close()`来等待一次循环内的所有任务结束

```cpp
thread_local seastar::semaphore limit(100);
seastar::future<> f() {
    return seastar::do_with(seastar::gate(), [] (auto& gate) {
        return seastar::do_for_each(
                boost::counting_iterator<int>(0),
                boost::counting_iterator<int>(456), 
                [&gate] (int i) {
                    return seastar::get_units(limit, 1).then([&gate] (auto units) {
                        gate.enter();
                        seastar::futurize_invoke(slow).finally([&gate, units = std::move(units)] {
                            gate.leave();
                        });
                    });
                }).finally([&gate] {
                    return gate.close();
                });
    });
}
```

`TODO`待补充内容（通过查看源码解释？）：

- 类似`get_unites()`的方式**RAII**管理`gate`代替显式的`gate.enter()/gate.leave()`
- semaphore等待者的**调度公平性问题**，总是顺序执行？
- **broken semaphore**的情况
- **互斥锁**的方式使用信号量`semaphore(1)`，当同一个线程多个任务会访问相同数据时，虽然不需要`std::mutex`一类的线程级互斥锁来保护，但需要协程级的互斥锁来保护
- **类似barrier**的方式使用信号量`semaphore(0)`，从0初始化，当需要执行N个前置任务才能正式执行一个任务时，每个任务对资源`signal(1)`并在正式任务前`wait(N)`

## 管道 Pipes

seastar提供`pipe<T>`来在两个任务中传输信息，`pipe<T>`有限缓存从而当满时producer侧就无法写入数据，空时consumer侧就无法取出数据，并且任意一侧可以主动关闭，另一侧就会收到`EOF`或是`Broken Pipe`，同时如果其中一侧关闭时阻塞的另一侧会被中断，并会一直阻塞

## 停止服务 Shutting down a service with a gate

假定一个服务随时都会有一些`slow()`任务正在运行，则想要良好停止服务时shutdown gracefully就需要**等待当前正在执行的任务结束，同时必须避免新的任务开始**

seastar提供了`seastar::gate`对象，可以用于服务的管理与优雅关闭，在一个操作开始前需要调用`gate::enter()`，在一个操作结束时需要调用`gate::leave()`，`gate`对象会管理当前正在运行的操作数量

当需要停止服务时可以调用`gate::close()`从而此后调用`gate::enter()`就会抛出异常`gate_closed_exception`阻止新任务开始运行，并且`gate::close()`返回的future会在当前正在运行的任务全都结束并调用`gate::leave()`后就绪，此时所有进展中的任务归零，服务可以优雅关闭

```cpp
seastar::future<> f() {
    return seastar::do_with(seastar::gate(), [] (auto& g) {
        return seastar::do_for_each(
                boost::counting_iterator<int>(1),
                boost::counting_iterator<int>(6),
                [&g] (int i) {
                    seastar::with_gate(g, [i] { return slow(i); });
                    // wait one second before starting the next iteration
                    return seastar::sleep(std::chrono::seconds(1));
                }).then([&g] {
                    seastar::sleep(std::chrono::seconds(1)).then([&g] {
                        // This will fail, because it will be after the close()
                        seastar::with_gate(g, [] { return slow(6); });
                    });
                    return g.close();
                });
    });
}
```

```text
starting 1
starting 2
starting 3 
starting 4
starting 5
WARNING: exceptional future ignored of type 'seastar::gate_closed_exception': gate closed
done 1
done 2
done 3
done 4
done 5
```

通常一个长久运行的服务并不会收到`gate::close()`的通知，因此必须要**主动去检查是否应该退出**，采用`gate::check()`来检查，假如已经关闭则检查就会抛出`gate_closed_exception`

```cpp
seastar::future<> slow(int i, seastar::gate &g) {
    std::cerr << "starting " << i << "\n";
    return seastar::do_for_each(
            boost::counting_iterator<int>(0),
            boost::counting_iterator<int>(10),
            [&g] (int) {
                g.check();
                return seastar::sleep(std::chrono::seconds(1));
            }).finally([i] {
                std::cerr << "done " << i << "\n";
            });
}
```

## 无共享 Introducing shared-nothing programming

[无共享设计](Shared_Nothing.md)

## 事件循环 More about Seastar's event loop

[事件循环 - Reactor模式](Reactor.md)

## 网络栈 Introducing Seastar's network stack

seastar提供了两种网络模块：Posix（基于Linux内核协议栈以及epoll）、native（基于L2 Ethernet使用DPDK重写了L3 TCP/IP层），网络协议栈同样遵循无共享的设计，**每个shard负责一部分连接**

seastar从main函数开始的主线程是一个shard，因此让每个shard都开始运行网络服务只需要采用线程间通信让每个shard都开始运行即可，每个shard从0开始，`seastar::smp::count`就是shard数量：

```cpp
seastar::future<> f() {
    return seastar::parallel_for_each(boost::irange<unsigned>(0, seastar::smp::count),
            [] (unsigned c) {
        return seastar::smp::submit_to(c, service_loop);
    });
}

seastar::future<> service_loop() {
    seastar::listen_options lo;
    lo.reuse_address = true;
    return seastar::do_with(seastar::listen(seastar::make_ipv4_address({1234}, lo)), [] (auto& listener) {
        return seastar::keep_doing([&listener] () {
            return listener.accept().then(
                [] (seastar::accept_result res) {
                    std::cout << "Accepted connection from " << res.remote_address << "\n";
            });
        });
    });
}
```

拓展到一个简单的echo服务器：

```cpp
seastar::future<> handle_connection(seastar::connected_socket s,
                                    seastar::socket_address a) {
    auto out = s.output(); // output_stream
    auto in = s.input();   // input_stream
    return do_with(std::move(s), std::move(out), std::move(in),
            [] (auto& s, auto& out, auto& in) {
        return seastar::repeat([&out, &in] {
            return in.read().then([&out] (auto buf) {
                if (buf) {
                    return out.write(std::move(buf)).then([&out] {
                        return out.flush();
                    }).then([] {
                        return seastar::stop_iteration::no;
                    });
                } else {
                    return seastar::make_ready_future<seastar::stop_iteration>(
                            seastar::stop_iteration::yes);
                }
            });
        }).then([&out] {
            return out.close();
        });
    });
}

seastar::future<> service_loop() {
    seastar::listen_options lo;
    lo.reuse_address = true;
    return seastar::do_with(seastar::listen(seastar::make_ipv4_address({1234}), lo),
            [] (auto& listener) {
        return seastar::keep_doing([&listener] () {
            return listener.accept().then(
                    [] (seastar::accept_result res) {
                // Note we ignore, not return, the future returned by
                // handle_connection(), so we do not wait for one
                // connection to be handled before accepting the next one.
                (void)handle_connection(std::move(res.connection), std::move(res.remote_address)).handle_exception(
                        [] (std::exception_ptr ep) {
                    fmt::print(stderr, "Could not handle connection: {}\n", ep);
                });
            });
        });
    });
}
```

采用**coroutine实现的echo服务器**（非官方教程给出，并不一定是最接近的实现）：

```cpp
seastar::future<> handle_connection(seastar::connected_socket s,
                                    seastar::socket_address a) {
  auto out = s.output();     // coroutine会管理栈上对象，从而免去do_with()
  auto in = s.input();
  while (true) {             // 普通while循环替换do_repeat()
    auto in_buf = co_await in.read();
    if (in_buf) {
      co_await out.write(std::move(in_buf));
      co_await out.flush();  // 采用co_await从而避免采用.then()
    } else {
      break;                 // 跳出循环不必再使用stop_iteration::yes
    }
  }
  co_await out.close();      // 必须在s存活时关闭连接，coroutine栈保证了这一点
  co_return;
}

seastar::future<> service_loop() {
  seastar::listen_options lo;
  lo.reuse_address = true;
  auto listener = seastar::listen(seastar::make_ipv4_address({8086}), lo);
  while (true) {
    auto res = co_await listener.accept();

    // handle_connection这里不能使用co_await，理由与非coroutine版本中的注释相同
    // 不需要等待当前connection结束，若有新连接可以直接开始处理
    (void)handle_connection(std::move(res.connection), std::move(res.remote_address))
        .handle_exception([] (std::exception_ptr ep) {
          fmt::print(stderr, "Could not handle connection: {}\n", ep);
        });
  }
}
```

## 分片服务 Sharded services

为了避免手动调用`seastar::smp::submit_to()`来实现每个shard上都执行相同的任务，seastar提供了分片服务的概念，即`seastar::sharded<T>`，会在每个shard上都创建一个`T`的副本，并提供了不同shard之间交互的方式，`T`唯一必须要提供的方法就是`stop()`

- `seastar::sharded<T>::start()`实际上只是将参数传给`T`的构造函数，在每个shard上构造出`T`的实例，而没有开始执行
- `seastar::sharded<T>::invoke_on_all()`就是在每个shard上都调用并传入该shard的`T`实例的引用，可以用于实际开启服务
- `seastar::sharded<T>::stop()`用于停止所有服务，其底层会调用`T::stop()`
- `seastar::sharded<T>::inboke_on()`可以在指定的shard上执行任务，传入该指定shard上的`T`实例的引用

```cpp
class my_service {
public:
    std::string _str;
    my_service(const std::string& str) : _str(str) { }
    seastar::future<> run() {
        std::cerr << "running on " << seastar::engine().cpu_id() <<
            ", _str = " << _str << "\n";
        return seastar::make_ready_future<>();
    }
    seastar::future<> stop() {
        return seastar::make_ready_future<>();
    }
};

seastar::sharded<my_service> s;

seastar::future<> f() {
    return s.start(std::string("hello")).then([] {
        return s.invoke_on_all([] (my_service& local_service) {
            return local_service.run();
        });
    }).then([] {
        return s.stop();
    });
}
```

## 干净的停止服务 Shutting down cleanly

`TODO: gate`

## 命令行选项 Command line options

seastar的命令行选项实际采用了`boost::program_options`，当需要添加自定义的命令行选项时（更详细的使用方式可以参考`boost::program_options`文档）：

```cpp
int main(int argc, char** argv) {
    seastar::app_template app;
    namespace bpo = boost::program_options;
    app.add_options({
        {"flag", "some optional flag"},
        {"size,s", bpo::value<int>()->default_value(100), "size"}
    });
    app.add_positional_options({
        {"filename", bpo::value<std::vector<seastar::sstring>>()->default_value({}), "sstable files to verify", -1}
    });
    app.run(argc, argv, [&app] {
        auto& args = app.configuration();
        if (args.count("flag")) {
            std::cout << "Flag is on\n";
        }
        std::cout << "Size is " << args["size"].as<int>() << "\n";
        auto& filenames = args["filename"].as<std::vector<seastar::sstring>>();
        for (auto&& fn : filenames) {
            std::cout << fn << "\n";
        }
        return seastar::make_ready_future<>();
    });
    return 0;
}
```

## 查错 Debugging a Seastar program

### 忽略的异常 Debugging ignored exceptions

通常所有异常都应该被显式的处理，如果存在一个`future`在析构时发现存储了一个异常没有被处理，就会输出一条包含调用栈地址的警告日志：

```log
WARN  2020-03-31 11:08:09,208 [shard 0] seastar - Exceptional future ignored: myexception, backtrace:   /lib64/libasan.so.5+0x6ce7f
  0x1a64193
  0x1a6265f
  0xf326cc
  0xeaf1a0
  0xeaffe4
  0xead7be
  0xeb5917
  0xee2477
  ...
```

seastar提供了一个工具可以用于翻译地址到函数调用（但要求对应的可执行文件例如下列命令中的`a.out`是**unstripped**，[关于stripped属性见此](https://linux.die.net/man/1/strip)，通常stripped可执行文件用于生产环境，而会保存一个unstripped副本用于翻译异常信息里的调用栈地址）：

```shell
seastar-addr2line -e a.out
```

随后将一系列地址信息复制进去并使用`ctrl+D`执行翻译，类似：

```cpp
void seastar::backtrace<seastar::current_backtrace()::{lambda(seastar::frame)#1}>(seastar::current_backtrace()::{lambda(seastar::frame)#1}&&) at include/seastar/util/backtrace.hh:56
seastar::current_backtrace() at src/util/backtrace.cc:84
seastar::report_failed_future(std::__exception_ptr::exception_ptr const&) at src/core/future.cc:116
seastar::future_state_base::~future_state_base() at include/seastar/core/future.hh:335
seastar::future_state<>::~future_state() at include/seastar/core/future.hh:414
 (inlined by) seastar::future<>::~future() at include/seastar/core/future.hh:990
f() at test.cc:12
...
```

很明显这种方式只是在**寻找一个被忽略的异常`future`的析构位置**，而这个**异常本身的来源并不能由此获得**，因此需要下面的方式来寻找

### 查找异常抛出点 Finding where an exception was thrown

由于C++不像Java等语言会保留异常时的调用栈，就需要程序员手动记录调用栈信息，采用`seastar::make_exception_future_with_backtrace`或是`seastar::throw_with_backtrace`来确保异常发生时可以找到抛出的位置：

```cpp
#include <util/backtrace.hh>
seastar::future<> g() {
    // seastar::throw_with_backtrace<std::runtime_error>("hello")
    return seastar::make_exception_future_with_backtrace<>(std::runtime_error("hello"));
}
```

随后此异常被忽略时输出的警告日志就包含了抛出异常位置的信息，也同样通过`seastar-addr2line`来转换获得

## Promise objects

`TODO: promise`

## 内存分配器 Memory allocation in Seastar

### Per-thread memory allocation

seastar的应用根据core分成多个shard，因此内存也同样分为多个shard独占，每个线程只会操作属于自身shard的内存，并且在初始内存划分时会**充分考虑到NUMA**

seastar重新实现了内存分配器`operator new/delete, malloc()/free()`及一系列相关函数，虽然都属于同一个进程，每个shard的操作线程能看到其他shard的数据，但是并不推荐直接通过这种方式进行数据修改，而是**尽可能每个线程只关注自身shard的数据**，在不可避免需要跨shard操作时采用seastar提供的[message passing方式](https://github.com/JasonYuchen/notes/blob/master/seastar/Message_Passing.md)（**不要使用共享内存进行通信，使用通信进行共享内存**）

### Foreign pointers

通常一个对象在某个shard上分配后，也应该由该shard所属线程进行析构回收，**seastar并不禁止跨线程的资源分配回收，但是效率更低**

采用`seastar::foreign_ptr<T>`可以将对象传递给另一个shard使用，**通常在跨线程时，会使用`seastar::foreign_ptr<std::unique_ptr<T>>`来代表独占所有权，从而在接收方获得对象的所有权并使用结束后，析构`seastar::foreign_ptr`并回到分配对象的线程进一步析构对象本身**，往往析构函数还包括访问源shard中的其他资源，因此析构函数在原线程上执行也非常重要

需要注意的是即使接收线程已经持有了对象的所有权，但是**对象的一些方法有可能访问了源shard中的其他数据，因此也只能通过原线程来执行**（如果能确定不会访问其他数据，例如读取对象的某个成员的值，就可以直接执行）：

```cpp
// fp is some foreign_ptr<>
return smp::submit_to(fp.get_owner_shard(), [p=fp.get()]
    { return p->some_method(); });
```

当需要在多个shard上共享数据时，则可以采用`seastar::foreign<seastar::lw_shared_ptr<T>>`，此时需要特别注意**避免多个线程同时修改该对象**，若需要修改则应该使用`submit_to`由原线程来执行修改

`seastar::foreign_ptr`通常只能被移动而不能被复制，假如持有的对象是智能指针`shared_ptr`这类可以被复制的对象，则可以调用`future<foreign_ptr> seastar::foreign_ptr::copy()`进行复制，此类复制是由原线程负责的，异步且低效

## `Seastar::thread`

C++20之后更推荐coroutine的方式写同步风格的代码，一个从`.then()`的异步风格转换为coroutines风格的[例子](https://github.com/scylladb/scylla/commit/865d07275622d07f21f9d825cbe157e873faa00c)

`TODO`

## 组件隔离 Isolation of application components
