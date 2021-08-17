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

```C++
seastar::future<> slow_op(std::vector<int> v) {
    // v is not copied again, but instead moved:
    return seastar::sleep(10ms).then([v = std::move(v)] { /* do something with v */ });
}
```

但这种场合适用范围有限，因为经常会遇到异步任务只需要某数据结构的一部分数据这种情况，若是`std::move`之后整个数据都只有某个异步任务持有，或是遇到调用者还需要保存数据的引用以备后续使用的场合这种方式也不适用

### 保持所有权 Keeping ownership at the caller

当以引用的方式传入对象给异步编程任务时，调用者必须确保直到任务结束，传入的引用始终有效，seastar提供了`do_with()`方法来确保传入的对象生命周期与任务一样，`do_with()`接受任意多的右值对象，并且后面的任务必须接受引用参数：

```C++
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

```C++
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

```C++
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

```C++
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

```C++
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

```C++
seastar::future<> flush_all_files(seastar::lw_shared_ptr<std::vector<seastar::file>> files, size_t max_concurrent) {
    return seastar::max_concurrent_for_each(files, max_concurrent, [] (seastar::file f) {
        return f.flush();
    });
}
```

## 等待多个事件 `when_all`：Waiting for multiple futures

采用`when_all()`可以发起并等待多个异步任务（必须是**右值**），并且这**一系列任务的结果是一个tuple会作为`when_all()`的返回值**，当不需要结果时可以使用`.discard_result()`，或是在下一个任务中使用这个tuple，如下：

```C++
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

```C++
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

```C++
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

```C++
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

```C++
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

```C++
seastar::future<> g() {
    static thread_local seastar::semaphore limit(100);
    return seastar::with_semaphore(limit, 1, [] {
        return slow(); // do the real work of g()
    });
}
```

另外也可以使用`seastar::get_unites()`返回特殊的unit对象来确保计数器正常：

```C++
seastar::future<> g() {
    static thread_local semaphore limit(100);
    return seastar::get_units(limit, 1).then([] (auto units) {
        return slow().finally([units = std::move(units)] {});
    });
}
```

### 限制资源使用 Limiting resource use

semaphore支持传入任意数值，因此也可以用来限制诸如内存等资源的使用：

```C++
seastar::future<> using_lots_of_memory(size_t bytes) {
    static thread_local seastar::semaphore limit(1000000); // limit to 1MB
    return seastar::with_semaphore(limit, bytes, [bytes] {
        // do something allocating 'bytes' bytes of memory
    });
}
```

### 限制循环并发数 Limiting parallelism of loops

同时执行的循环的并发数也可以通过semaphore来限制，一个简单的循环如下没有任何并发，下一个`slow()`每次都在前一个`slow()`结束时才会开始：

```C++
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

```C++
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

```C++
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

```C++
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
- 每个`slow()`在执行前后需要`gate.enter()/gate.leave()`（是否也可以RAII？），并且最后通过`gate.close()`来等待一次循环内的所有任务结束

```C++
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

## 无共享 Introducing shared-nothing programming

[无共享设计](Shared_Nothing.md)

## 事件循环 More about Seastar's event loop

[事件循环 - Reactor模式](Reactor.md)

## 网络栈 Introducing Seastar's network stack

## 分片服务 Sharded services

## 干净的停止服务 Shutting down cleanly

## 命令行选项 Command line options

## 查错 Debugging a Seastar program

## Promise objects

## 内存分配器 Memory allocation in Seastar

## `Seastar::thread`

## 组件隔离 Isolation of application components
