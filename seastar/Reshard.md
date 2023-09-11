# Some notes about resharding in Seastar-based applications

Seastar采用了thread-per-shard/core的设计，在基于Seastar编写应用程序时往往也会从day 0开始[sharding all the way](Shared_Nothing.md)从而最大化这种设计的性能

传统的架构设计中往往会利用多个线程加速处理，但是底层的数据和文件则是所有线程共享并通过锁等方式来同步，从而处理的线程数可以动态（运行中或是重启）调整

而在Seastar的应用程序中，随意调整shards的数量并不容易，通常每个shard处理了一组数据和文件，一旦reshard后可能需要**重平衡rebalance**，这虽然是分布式系统下常见的策略（例如基于一致性散列consistent hashing来分区数据并重平衡），但在单机多核中实现则较为复杂，以ScyllaDB的commitlog为例：

**基本恢复流程就是在shard 0上获得所有日志文件，按当前的sharding模式均分给所有shards进行日志文件的并行读取和解析，每个shard对解析出来的每一条数据重新计算出现在所属的shard（解析的shard不一定就是数据所属的shard），将数据发送到其所属的shard上进行应用**

1. 系统启动时的一个环节就是[初始化commit log](https://github.com/scylladb/scylla/blob/44f4ea38c5b70e80dfeb2962d21447b3c53dda49/main.cc#L971)，**找到当前有的日志进行重放，注意这一步都是在shard 0上执行**

    - 调用`commitlog::get_segments_to_replay`获得数据目录下所有日志的文件列表
    - 初始化`commitlog_replayer`
    - 调用`commitlog_replayer::recover`将日志文件列表传入并进行回放

    ```cpp
    // scylla/main.cc
    auto cl = db.local().commitlog();
    if (cl != nullptr) {
        auto paths = cl->get_segments_to_replay();
        if (!paths.empty()) {
            supervisor::notify("replaying commit log");
            auto rp = db::commitlog_replayer::create_replayer(db).get0();
            rp.recover(paths, db::commitlog::descriptor::FILENAME_PREFIX).get();
            supervisor::notify("replaying commit log - flushing memtables");
            db.invoke_on_all([] (database& db) {
                return db.flush_all_memtables();
            }).get();
            supervisor::notify("replaying commit log - removing old commitlog segments");
            //FIXME: discarded future
            (void)cl->delete_segments(std::move(paths));
        }
    }
    ```

2. [回放过程](https://github.com/scylladb/scylla/blob/44f4ea38c5b70e80dfeb2962d21447b3c53dda49/db/commitlog/commitlog_replayer.cc#L355)中，针对不同的日志文件计算出原先属于哪个shard，而重启后（shard数量可能改变）属于哪个shard，**采用`submit_to`发送到新shard上进行回放**

    - 首先构造`commitlog::descriptor`并根据文件信息确认由哪个新shard负责回放，构造所属shard到日志文件的映射关系`shard_file_map`
    - 在`commitlog_replayer::recover`种会采用`map_reduce`的方法由所有shards共同参与recover，且采用`smp::submit_to`确保每个shard负责的文件都是`shard_file_map`中属于该shard的部分，即代码中的`auto range = map->equal_range(id)`
    - 对每个文件依次调用`commitlog_replayer::impl::recover`执行解析和恢复，实际工作主要在`commitlog_replayer::impl::process`中完成

    ```cpp
    // scylla/db/commitlog/commitlog_replayer.cc
    future<> db::commitlog_replayer::recover(std::vector<sstring> files, sstring fname_prefix) {
        typedef std::unordered_multimap<unsigned, sstring> shard_file_map;

        // pre-compute work per shard already.
        auto map = ::make_lw_shared<shard_file_map>();
        for (auto& f : files) {
            commitlog::descriptor d(f, fname_prefix);
            replay_position p = d;
            map->emplace(p.shard_id() % smp::count, std::move(f));
        }

        return do_with(std::move(fname_prefix), [this, map] (sstring& fname_prefix) {
            return _impl->start().then([this, map, &fname_prefix] {
                return map_reduce(smp::all_cpus(), [this, map, &fname_prefix] (unsigned id) {
                    return smp::submit_to(id, [this, id, map, &fname_prefix] () {
                        auto total = ::make_lw_shared<impl::stats>();
                        // TODO: or something. For now, we do this serialized per shard,
                        // to reduce mutation congestion. We could probably (says avi)
                        // do 2 segments in parallel or something, but lets use this first.
                        auto range = map->equal_range(id);
                        return do_for_each(range.first, range.second, [this, total, &fname_prefix] (const std::pair<unsigned, sstring>& p) {
                            auto&f = p.second;
                            rlogger.debug("Replaying {}", f);
                            return _impl->recover(f, fname_prefix).then([f, total](impl::stats stats) {
                                // skipping
                            });
                        }).then([total] {
                            return make_ready_future<impl::stats>(*total);
                        });
                    });
                }, impl::stats(), std::plus<impl::stats>()).then([](impl::stats totals) {
                    // skipping
                });
            }).finally([this] {
                return _impl->stop();
            });
        });
    }
    ```

    ```cpp
    // scylla/db/commitlog/commitlog_replayer.cc
    future<db::commitlog_replayer::impl::stats>
    db::commitlog_replayer::impl::recover(sstring file, const sstring& fname_prefix) const {
        assert(_column_mappings.local_is_initialized());

        replay_position rp{commitlog::descriptor(file, fname_prefix)};
        auto gp = min_pos(rp.shard_id());

        if (rp.id < gp.id) {
            rlogger.debug("skipping replay of fully-flushed {}", file);
            return make_ready_future<stats>();
        }
        position_type p = 0;
        if (rp.id == gp.id) {
            p = gp.pos;
        }

        auto s = make_lw_shared<stats>();
        auto& exts = _db.local().extensions();

        return db::commitlog::read_log_file(file, fname_prefix, service::get_local_commitlog_priority(),
                std::bind(&impl::process, this, s.get(), std::placeholders::_1),
                p, &exts).then_wrapped([s](future<> f) {
            // skipping
        });
    }
    ```

3. 原先不同shard写入不同日志文件的数据可能也是交织的，因此在新shard上进行回放时，依然又可能需要**将具体的log entry发送给其他shard**进行处理，对`sharded<T>`可以采用`invoke_on`

    - 首先对日志内容进行解析，构造`commitlog_entry_reader`
    - 使用`_db.local().shard_of(fm)`获得每一个log entry相应数据所属的新shard，并使用`_db.invoke_on(shard, ...)`将数据发送到对应的shard上应用

    ```cpp
    future<> db::commitlog_replayer::impl::process(stats* s, commitlog::buffer_and_replay_position buf_rp) const {
        auto&& buf = buf_rp.buffer;
        auto&& rp = buf_rp.position;
        try {
            commitlog_entry_reader cer(buf);
            auto& fm = cer.mutation();
            // skipping
            auto shard = _db.local().shard_of(fm);
            return _db.invoke_on(shard, [this, cer = std::move(cer), &src_cm, rp, shard, s] (database& db) mutable -> future<> {
                auto& fm = cer.mutation();
                // skipping
            }).then_wrapped([s] (future<> f) {
                // skipping
            });
        } catch (no_such_column_family&) {
            // No such CF now? Origin just ignores this.
        } catch (...) {
            s->invalid_mutations++;
            // TODO: write mutation to file like origin.
            rlogger.warn("error replaying: {}", std::current_exception());
        }

        return make_ready_future<>();
    }
    ```
