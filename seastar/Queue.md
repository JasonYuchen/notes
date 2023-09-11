# seastar的队列

seastar是一个基于continuation的异步编程框架，而从C++ 20开始，seastar提供了[对协程的支持](Coroutines.md)，并且seastar有支持协程并发的单生产者单消费者队列（SPSC queue），注意与seastar用于跨线程通信的队列相区分，[跨线程通信的分析见此](Message_Passing.md)

## 核心原理

由于seastar的架构被设计为[thread-per-core/shard设计](Shared_Nothing.md)，因此该队列也仅在单个线程内使用，其实现与基于锁/原子变量的线程安全并发队列有所不同，其**核心原理在于采用`promise/future`组合实现协程的切换**代替了`mutex/conditional_varaible`组合，如下：

*这也是一个通过`promise`和`future`来实现协程间协作的范例*

```cpp
template <typename T>
class queue {
  std::optional<promise<>> _not_empty;
  std::optional<promise<>> _not_full;
};
```

- **当队列已满时**
  - 直接`push()`元素会失败
  - **选择`push_eventually()`就会被`not_full()`挂起**直到有元素被移除，`not_full()`中生成一对`promise`和`future`，当队列存在空间时`promise`就会满足，对应的`future`就绪后原先的`push_eventually()`成功
  
  ```cpp
  template <typename T>
  inline future<> queue<T>::push_eventually(T&& data) {
      if (_ex) {
          return make_exception_future<>(_ex);
      }
      if (full()) {
          return not_full().then([this, data = std::move(data)] () mutable {
              _q.push(std::move(data));
              notify_not_empty();
          });
      } else {
          _q.push(std::move(data));
          notify_not_empty();
          return make_ready_future<>();
      }
  }

  template <typename T>
  inline future<> queue<T>::not_full() {
      if (_ex) {
          return make_exception_future<>(_ex);
      }
      if (!full()) {
          return make_ready_future<>();
      } else {
          _not_full = promise<>();
          return _not_full->get_future();
      }
  }
  ```

- **当队列在满的状态下移除元素时**
  此时假如有协程先前调用了`push_eventually()`则`_not_full`非空，此时`pop()`或`pop_eventually()`都会通过`notify_not_full()`使被挂起的协程可以继续执行（但是此时还不会立即切换到该协程，而是继续移除出元素）

  **通过判断`_not_full`是否为空就知道是否有协程在等待**，直接`_not_full->set_value()`使得等待的协程就绪，随后可以被运行，同时清空`_not_full`

  ```cpp
  template <typename T>
  inline T queue<T>::pop() {
      if (_q.size() == _max) {
          notify_not_full();
      }
      T data = std::move(_q.front());
      _q.pop();
      return data;
  }

  template <typename T>
  inline void queue<T>::notify_not_full() {
      if (_not_full) {
          _not_full->set_value();
          _not_full = std::optional<promise<>>();
      }
  }
  ```

- **当队列为空时**
  流程类似，略过
- **当队列为空的状态下加入元素时**
  流程类似，略过

## 示例

需要注意的是`producer`和`consumer`都可以不等待，只需要**在reactor引擎退出前确保这两个协程都已经结束**即可，否则ASAN会报错memory leak

```cpp
seastar::future<> producer(seastar::queue<int>& q) {
  for (int i = 0; i < 5; ++i) {
    std::cout << "producer " << i << std::endl;
    co_await q.push_eventually(int{i});
    co_await seastar::sleep(1s);
  }
  co_return;
}

seastar::future<> consumer(seastar::queue<int>& q) {
  int collected = 0;
  while (true) {
    auto i = co_await q.pop_eventually();
    std::cout << "consumer " << i << std::endl;
    collected++;
    if (collected >= 5) {
      break;
    }
  }
  co_return;
}

int main(int argc, char** argv) {
  seastar::app_template app;
  app.run(argc, argv, [] () -> seastar::future<> {
    seastar::queue<int> q(10);
    auto f = producer(q);
    co_await consumer(q);
    // must wait producer, it may still in the sleep when the reactor exits
    co_await f.discard_result();
    std::cout << "end" << std::endl;
  });
}
```

输出如下：

```text
producer 0
consumer 0
producer 1
consumer 1
producer 2
consumer 2
producer 3
consumer 3
producer 4
consumer 4
end
```

## 完整代码

```cpp
/// Asynchronous single-producer single-consumer queue with limited capacity.
/// There can be at most one producer-side and at most one consumer-side operation active at any time.
/// Operations returning a future are considered to be active until the future resolves.
template <typename T>
class queue {
    std::queue<T, circular_buffer<T>> _q;
    size_t _max;
    std::optional<promise<>> _not_empty;
    std::optional<promise<>> _not_full;
    std::exception_ptr _ex = nullptr;
private:
    void notify_not_empty();
    void notify_not_full();
public:
    explicit queue(size_t size);

    /// \brief Push an item.
    ///
    /// Returns false if the queue was full and the item was not pushed.
    bool push(T&& a);

    /// \brief Pop an item.
    ///
    /// Popping from an empty queue will result in undefined behavior.
    T pop();

    /// \brief access the front element in the queue
    ///
    /// Accessing the front of an empty queue will result in undefined
    /// behaviour.
    T& front();

    /// Consumes items from the queue, passing them to \c func, until \c func
    /// returns false or the queue it empty
    ///
    /// Returns false if func returned false.
    template <typename Func>
    bool consume(Func&& func);

    /// Returns true when the queue is empty.
    bool empty() const;

    /// Returns true when the queue is full.
    bool full() const;

    /// Returns a future<> that becomes available when pop() or consume()
    /// can be called.
    /// A consumer-side operation. Cannot be called concurrently with other consumer-side operations.
    future<> not_empty();

    /// Returns a future<> that becomes available when push() can be called.
    /// A producer-side operation. Cannot be called concurrently with other producer-side operations.
    future<> not_full();

    /// Pops element now or when there is some. Returns a future that becomes
    /// available when some element is available.
    /// If the queue is, or already was, abort()ed, the future resolves with
    /// the exception provided to abort().
    /// A consumer-side operation. Cannot be called concurrently with other consumer-side operations.
    future<T> pop_eventually();

    /// Pushes the element now or when there is room. Returns a future<> which
    /// resolves when data was pushed.
    /// If the queue is, or already was, abort()ed, the future resolves with
    /// the exception provided to abort().
    /// A producer-side operation. Cannot be called concurrently with other producer-side operations.
    future<> push_eventually(T&& data);

    /// Returns the number of items currently in the queue.
    size_t size() const { return _q.size(); }

    /// Returns the size limit imposed on the queue during its construction
    /// or by a call to set_max_size(). If the queue contains max_size()
    /// items (or more), further items cannot be pushed until some are popped.
    size_t max_size() const { return _max; }

    /// Set the maximum size to a new value. If the queue's max size is reduced,
    /// items already in the queue will not be expunged and the queue will be temporarily
    /// bigger than its max_size.
    void set_max_size(size_t max) {
        _max = max;
        if (!full()) {
            notify_not_full();
        }
    }

    /// Destroy any items in the queue, and pass the provided exception to any
    /// waiting readers or writers - or to any later read or write attempts.
    void abort(std::exception_ptr ex) {
        while (!_q.empty()) {
            _q.pop();
        }
        _ex = ex;
        if (_not_full) {
            _not_full->set_exception(ex);
            _not_full= std::nullopt;
        }
        if (_not_empty) {
            _not_empty->set_exception(std::move(ex));
            _not_empty = std::nullopt;
        }
    }

    /// \brief Check if there is an active consumer
    ///
    /// Returns true if another fiber waits for an item to be pushed into the queue
    bool has_blocked_consumer() const {
        return bool(_not_empty);
    }
};

template <typename T>
inline
queue<T>::queue(size_t size)
    : _max(size) {
}

template <typename T>
inline
void queue<T>::notify_not_empty() {
    if (_not_empty) {
        _not_empty->set_value();
        _not_empty = std::optional<promise<>>();
    }
}

template <typename T>
inline
void queue<T>::notify_not_full() {
    if (_not_full) {
        _not_full->set_value();
        _not_full = std::optional<promise<>>();
    }
}

template <typename T>
inline
bool queue<T>::push(T&& data) {
    if (_q.size() < _max) {
        _q.push(std::move(data));
        notify_not_empty();
        return true;
    } else {
        return false;
    }
}

template <typename T>
inline
T& queue<T>::front() {
    return _q.front();
}

template <typename T>
inline
T queue<T>::pop() {
    if (_q.size() == _max) {
        notify_not_full();
    }
    T data = std::move(_q.front());
    _q.pop();
    return data;
}

template <typename T>
inline
future<T> queue<T>::pop_eventually() {
    if (_ex) {
        return make_exception_future<T>(_ex);
    }
    if (empty()) {
        return not_empty().then([this] {
            if (_ex) {
                return make_exception_future<T>(_ex);
            } else {
                return make_ready_future<T>(pop());
            }
        });
    } else {
        return make_ready_future<T>(pop());
    }
}

template <typename T>
inline
future<> queue<T>::push_eventually(T&& data) {
    if (_ex) {
        return make_exception_future<>(_ex);
    }
    if (full()) {
        return not_full().then([this, data = std::move(data)] () mutable {
            _q.push(std::move(data));
            notify_not_empty();
        });
    } else {
        _q.push(std::move(data));
        notify_not_empty();
        return make_ready_future<>();
    }
}

template <typename T>
template <typename Func>
inline
bool queue<T>::consume(Func&& func) {
    if (_ex) {
        std::rethrow_exception(_ex);
    }
    bool running = true;
    while (!_q.empty() && running) {
        running = func(std::move(_q.front()));
        _q.pop();
    }
    if (!full()) {
        notify_not_full();
    }
    return running;
}

template <typename T>
inline
bool queue<T>::empty() const {
    return _q.empty();
}

template <typename T>
inline
bool queue<T>::full() const {
    return _q.size() >= _max;
}

template <typename T>
inline
future<> queue<T>::not_empty() {
    if (_ex) {
        return make_exception_future<>(_ex);
    }
    if (!empty()) {
        return make_ready_future<>();
    } else {
        _not_empty = promise<>();
        return _not_empty->get_future();
    }
}

template <typename T>
inline
future<> queue<T>::not_full() {
    if (_ex) {
        return make_exception_future<>(_ex);
    }
    if (!full()) {
        return make_ready_future<>();
    } else {
        _not_full = promise<>();
        return _not_full->get_future();
    }
}
```
