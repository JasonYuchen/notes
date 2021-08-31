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

```C++
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

```C++
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

## Workflow: set an object

```C++
// TODO
```
