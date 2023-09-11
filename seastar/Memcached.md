# Seastar APP - Memcached

从seastar官方提供的[memcached](https://github.com/scylladb/seastar/tree/master/apps/memcached)来看如何用seastar构建一个应用，并尝试采用C++ 20 coroutine的方式重写memcached

## Ragel

seastar采用了[Ragel](https://en.wikipedia.org/wiki/Ragel)来生成`ascii.hh`

`TODO: explain ascii.rl`

## Classes

- `struct expiration`
  - `ever_expires`
  - `to_time_point`
- `class item : public slab_item_base`
  公有方法：
  - `get_timeout`
  - `version`
  - `key`
  - `ascii_prefix`
  - `value`
  - `key_size`
  - `ascii_prefix_size`
  - `value_size`
  - `data_as_integral`
  - `cancel`
  - `get_slab_page_index`
  - `is_unlocked`
  - `intrusive_ptr_add_ref`
  - `intrusive_ptr_release`
- `class item_key`
  - `hash`
  - `key`
- `struct item_key_cmp`
  - `compare`
- `struct cache_stats`
- `struct item_insertion_data`
- `class cache`
  公有方法：
  - `flush_all`
  - `flush_at`
  - `set`
  - `add`
  - `replace`
  - `remove`
  - `get`
  - `cas`
  - `size`
  - `bucket_count`
  - `stats`
  - `incr`
  - `decr`
  - `print_hash_stats`
  - `stop`
  - `get_wc_to_clock_type_delta`
  私有方法：
  - `item_size`
  - `erase`
  - `expire`
  - `find`
  - `add_overriding`
  - `add_new`
  - `maybe_rehash`
- `class sharded_cache`
  公有方法：
  - `flush_all`
  - `flush_at`
  - `set`
  - `add`
  - `replace`
  - `remove`
  - `get`
  - `cas`
  - `stats`
  - `incr`
  - `decr`
  - `print_hash_stats`
  - `get_wc_to_clock_type_delta`
  私有方法：
  - `get_cpu`
- `struct system_stats`
- `class ascii_protocol`
  公有方法：
  - `handle`
  私有方法：
  - `append_item`
  - `handle_get`
- `class udp_server`
- `class tcp_server`
- `class stats_printer`

## Workflow: start a server

[参考教程中的`seastar::sharded`](https://github.com/JasonYuchen/notes/blob/master/seastar/Comprehensive_Tutorial.md#%E5%88%86%E7%89%87%E6%9C%8D%E5%8A%A1-sharded-services)

```cpp
int main(int ac, char** av) {
    // distributed等同于seastar::sharded
    distributed<memcache::cache> cache_peers;
    memcache::sharded_cache cache(cache_peers);
    distributed<memcache::system_stats> system_stats;
    distributed<memcache::udp_server> udp_server;
    distributed<memcache::tcp_server> tcp_server;
    memcache::stats_printer stats(cache);

    namespace bpo = boost::program_options;
    app_template app;
    app.add_options()
        ("max-datagram-size", bpo::value<int>()->default_value(memcache::udp_server::default_max_datagram_size),
             "Maximum size of UDP datagram")
        // ... 略去各种自定义参数
        ;

    return app.run_deprecated(ac, av, [&] {
        // 注册退出时的回调，会依次调用多个回调
        engine().at_exit([&] { return tcp_server.stop(); });
        engine().at_exit([&] { return udp_server.stop(); });
        engine().at_exit([&] { return cache_peers.stop(); });
        engine().at_exit([&] { return system_stats.stop(); });

        auto&& config = app.configuration();
        uint16_t port = config["port"].as<uint16_t>();
        uint64_t per_cpu_slab_size = config["max-slab-size"].as<uint64_t>() * MB;
        uint64_t slab_page_size = config["slab-page-size"].as<uint64_t>() * MB;
        // 依次调用start初始化相应的server，并随后使用invoke_on_all真正启动server
        return cache_peers.start(std::move(per_cpu_slab_size), std::move(slab_page_size)).then([&system_stats] {
            return system_stats.start(memcache::clock_type::now());
        }).then([&] {
            std::cout << PLATFORM << " memcached " << VERSION << "\n";
            return make_ready_future<>();
        }).then([&, port] {
            return tcp_server.start(std::ref(cache), std::ref(system_stats), port);
        }).then([&tcp_server] {
            return tcp_server.invoke_on_all(&memcache::tcp_server::start);
        }).then([&, port] {
            if (engine().net().has_per_core_namespace()) {
                return udp_server.start(std::ref(cache), std::ref(system_stats), port);
            } else {
                return udp_server.start_single(std::ref(cache), std::ref(system_stats), port);
            }
        }).then([&] {
            return udp_server.invoke_on_all(&memcache::udp_server::set_max_datagram_size,
                    (size_t)config["max-datagram-size"].as<int>());
        }).then([&] {
            return udp_server.invoke_on_all(&memcache::udp_server::start);
        }).then([&stats, start_stats = config.count("stats")] {
            if (start_stats) {
                stats.start();
            }
        });
    });
}
```

改写为**C++20 Coroutine的实现**，一些要点：

- 采用`app.run()`的启动方式，取代过时的`app.run_depreacted()`
- 显式处理SIGINT/SIGTERM信号用于服务的终止，默认情况下这两个信号会直接调用`engine().stop()`，在`co_await stop_signal.wait();`返回时就会关闭所有服务
- 采用协程的方式停止服务而不是使用`engine().at_exit()`，相关讨论[见此](https://github.com/scylladb/scylla/issues/293)，即接收信号后按顺序`co_await tcp_server.stop();`的方式逐个停止服务
- 捕获所有初始化阶段的异常
- 相比较continuation的方式（大量使用`.then()`），coroutine的方式更加直观贴近同步的写法，不易出错

```cpp
int main(int ac, char **av)
{
  namespace bpo = boost::program_options;
  app_template::config app_cfg;
  app_cfg.auto_handle_sigint_sigterm = false;
  app_template app(std::move(app_cfg));
  app.add_options() // 配置项不变

  // 协程写在lambda函数内时必须写出返回类型，不支持自动推导返回类型
  return app.run(ac, av, [&]() -> future<>
  {
    try {
      memcache::stop_signal stop_signal;
      sharded<memcache::cache> cache_peers;
      memcache::sharded_cache cache(cache_peers);
      sharded<memcache::system_stats> system_stats;
      sharded<memcache::udp_server> udp_server;
      sharded<memcache::tcp_server> tcp_server;
      memcache::stats_printer stats(cache);

      auto &&config = app.configuration();
      uint16_t port = config["port"].as<uint16_t>();
      uint64_t per_cpu_slab_size = config["max-slab-size"].as<uint64_t>() * MB;
      uint64_t slab_page_size = config["slab-page-size"].as<uint64_t>() * MB;
      size_t max_datagram_size = config["max-datagram-size"].as<size_t>();

      l.info("{} memcached {}", PLATFORM, VERSION);
      co_await cache_peers.start(per_cpu_slab_size, slab_page_size);
      co_await system_stats.start(memcache::clock_type::now());
      co_await tcp_server.start(std::ref(cache), std::ref(system_stats), port);
      co_await tcp_server.invoke_on_all(&memcache::tcp_server::start);
      if (engine().net().has_per_core_namespace()) {
        co_await udp_server.start(std::ref(cache),
                                  std::ref(system_stats),
                                  port);
      } else {
        co_await udp_server.start_single(std::ref(cache),
                                         std::ref(system_stats),
                                         port);
      }
      co_await udp_server.invoke_on_all(&memcache::udp_server::set_max_datagram_size,
                                        max_datagram_size);
      co_await udp_server.invoke_on_all(&memcache::udp_server::start);
      if (config.count("stats")) {
        (void) stats.start();
      }
      co_await stop_signal.wait();
      co_await udp_server.stop();
      co_await tcp_server.stop();
      co_await system_stats.stop();
      co_await cache_peers.stop();
    } catch (...) {
      l.error("init failed: {}", std::current_exception());
    }
  });
}
```

## Workflow: TCP server

采用C++20 Coroutine极大的简化了异步代码的编写复杂性，逻辑更为直观和内聚

```cpp
class tcp_server {
 private:
  // 用于在停止服务时等待最后一个连接处理结束
  std::optional<future<>> _task;
  lw_shared_ptr<seastar::server_socket> _listener;
  sharded_cache &_cache;
  distributed<system_stats> &_system_stats;
  uint16_t _port;
  struct connection {
    connected_socket _socket;
    socket_address _addr;
    input_stream<char> _in;
    output_stream<char> _out;
    ascii_protocol _proto;
    distributed<system_stats> &_system_stats;
    connection(connected_socket &&socket,
               socket_address addr,
               sharded_cache &c,
               distributed<system_stats> &system_stats)
      : _socket(std::move(socket)),
        _addr(addr),
        _in(_socket.input()),
        _out(_socket.output()),
        _proto(c, system_stats),
        _system_stats(system_stats)
    {
      _system_stats.local()._curr_connections++;
      _system_stats.local()._total_connections++;
    }
    static future<> handle(lw_shared_ptr<connection> conn)
    {
      // eof()意味着对端关闭连接，此时可以安全退出
      while (!conn->_in.eof()) {
        try {
          co_await conn->_proto.handle(conn->_in, conn->_out);
          co_await conn->_out.flush();
        } catch (...) {
          // 任意异常都会引起连接关闭
          l.warn("connection {} closed: exception {}",
                 conn->_addr,
                 std::current_exception());
          break;
        }
      }
      co_await conn->_out.close();
      co_return;
    }
    ~connection()
    {
      _system_stats.local()._curr_connections--;
    }
  };
 public:
  tcp_server(sharded_cache &cache,
             distributed<system_stats> &system_stats,
             uint16_t port = 11211)
    : _cache(cache), _system_stats(system_stats), _port(port)
  {}

  future<> start()
  {
    // Run in the background.
    _task = process();
    l.info("tcp server started");
    co_return;
  }

  future<> stop()
  {
    // 此时会触发_listener->accept()抛出异常，并引起process()循环的退出
    _listener->abort_accept();
    // 等待process()循环结束，不应该有任何异常因此可以discard
    co_await _task->discard_result();
    l.info("tcp server stopped");
    co_return;
  }

  future<> process()
  {
    try {
      listen_options lo;
      // 启用地址重用，从而服务重启时可以监听相同的端口
      lo.reuse_address = true;
      _listener =
        seastar::server_socket(seastar::listen(make_ipv4_address({_port}), lo));
      while (true) {
        auto ar = co_await _listener->accept();
        auto conn = make_lw_shared<connection>(std::move(ar.connection),
                                               std::move(ar.remote_address),
                                               _cache,
                                               _system_stats);
        // Run in the background until eof has reached on the input connection.
        // 每个连接单独处理，不等待当前连接处理结束就立即开始准备accept()新连接
        (void) connection::handle(conn);
      }
    } catch (...) {
      l.error("tcp server exited: exception {}", std::current_exception());
    }
    co_return;
  }
};
```

## Workflow: protocol

在TCP server中使用，根据当前连接收到的数据状态（`_parser._state`）构造状态机（底层使用`sharded_cache`）来服务一条连接，而UDP server则是直接使用`sharded_cache`

```cpp
future<> ascii_protocol::handle(input_stream<char>& in, output_stream<char>& out) {
    _parser.init();
    return in.consume(_parser).then([this, &out] () -> future<> {
        switch (_parser._state) {
            case memcache_ascii_parser::state::eof:
                return make_ready_future<>();

            case memcache_ascii_parser::state::error:
                return out.write(msg_error);

            case memcache_ascii_parser::state::cmd_set:
            {
                _system_stats.local()._cmd_set++;
                prepare_insertion();
                // 调用底层的sharded_cache完成真正的set
                auto f = _cache.set(_insertion);
                if (_parser._noreply) {
                    return std::move(f).discard_result();
                }
                return std::move(f).then([&out] (...) {
                    return out.write(msg_stored);
                });
            }
            // SKIP various cases
        };
        // 解析状态异常，直接退出，是否断开当前连接比退出更好？
        std::abort();
    }).then_wrapped([this, &out] (auto&& f) -> future<> {
        // FIXME: then_wrapped() being scheduled even though no exception was triggered has a
        // performance cost of about 2.6%. Not using it means maintainability penalty.
        try {
            f.get();
        } catch (std::bad_alloc& e) {
            if (_parser._noreply) {
                return make_ready_future<>();
            }
            return out.write(msg_out_of_memory);
        }
        return make_ready_future<>();
    });
};

// The caller must keep @insertion live until the resulting future resolves.
// insertion被保存在ascii_protocol对象内的_insertion内，一直有效
future<bool> sharded_cache::set(item_insertion_data& insertion) {
    // 通过对对象的key进行分区获得对应的shard位置
    auto cpu = get_cpu(insertion.key);
    if (this_shard_id() == cpu) {
        return make_ready_future<bool>(_peers.local().set(insertion));
    }
    // 若是其他shard，则通过消息通信将insertion发送到对应的shard
    // remote_origin_tag则是tag dispatch，通过这个tag来确定是否可以持有insertion的所有权（并move）
    return _peers.invoke_on(cpu, &cache::set<remote_origin_tag>, std::ref(insertion));
}

struct remote_origin_tag {
  template<typename T>
  static inline
  T move_if_local(T &ref)
  {
    // 对于remote来说拷贝一份而不move
    return ref;
  }
};

struct local_origin_tag {
  template<typename T>
  static inline
  T move_if_local(T &ref)
  {
    // 对于local来说可以安全的直接move走对象
    return std::move(ref);
  }
};

inline
unsigned sharded_cache::get_cpu(const item_key &key)
{
  // 根据key获得shard的方式就是简单的hash partition方法
  // 其他还可以考虑range partition方法
  return std::hash<item_key>()(key) % smp::count;
}
```
