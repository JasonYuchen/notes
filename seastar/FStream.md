# seastar的文件流

## File input stream and read ahead

seastar支持DMA方式的磁盘IO，即`O_DIRECT`，则在读取文件数据时会绕过OS的page cache，那么当需要连续读取时往往性能就会弱于有page cache的方式，[参考此处](https://github.com/JasonYuchen/notes/blob/master/linux/io_uring.md#modern-storage-is-plenty-fast-it-is-the-apis-that-are-bad)

seastar的input file stream**提供了read ahead的选项并内建了缓存**从而能够达到更高的连续读取表现，并且在此之上，seastar提供了动态反馈调节，根据访问模式对read ahead的行为进行动态调整，深入优化了IO的表现，这一节从源码的角度来看seastar的做法，其input stream的实现、内建缓存与读取时的行为、动态调整的思路

### File input stream interface

- 核心接口就是`make_file_input_stream`，并且由于内建了缓存支持，这里的`offset`不需要caller对齐，会在实际读取时进行对齐处理
- 在`file_input_stream_options`中可以看出对一个文件输入流来说可以指定缓存大小、read_ahead的IO操作数量、调度的优先级（具体分析[见IO Scheduler](https://github.com/JasonYuchen/notes/blob/master/seastar/Disk_IO_Scheduler.md#%E4%BC%98%E5%85%88%E7%BA%A7-priority-classes-in-scylla)）以及动态调整选项
- 对于动态调整选项而言，初始可以采用默认空历史也可以显式给定历史IO情况以更好的指导input stream的read ahead行为
- 对于file input stream转换为input stream的过程略`TODO`

```cpp
// seastar/core/fstream.hh
input_stream<char> make_file_input_stream(
    file file, uint64_t offset, uint64_t len, file_input_stream_options options = {});

/// Data structure describing options for opening a file input stream
struct file_input_stream_options {
    size_t buffer_size = 8192;    ///< I/O buffer size
    unsigned read_ahead = 0;      ///< Maximum number of extra read-ahead operations
    ::seastar::io_priority_class io_priority_class = default_priority_class();
    lw_shared_ptr<file_input_stream_history> dynamic_adjustments = { }; ///< Input stream history, if null dynamic adjustments are disabled
};

class file_input_stream_history {
    static constexpr uint64_t window_size = 4 * 1024 * 1024;
    struct window {
        uint64_t total_read = 0;
        uint64_t unused_read = 0;
    };
    window current_window;
    window previous_window;
    unsigned read_ahead = 1;

    friend class file_data_source_impl;
};

class data_source_impl {
public:
    virtual ~data_source_impl() {}
    virtual future<temporary_buffer<char>> get() = 0;
    virtual future<temporary_buffer<char>> skip(uint64_t n);
    virtual future<> close() { return make_ready_future<>(); }
};
```

### File input stream implementation

1. **基本读取流程**
    - `struct issued_read`保存了单次真正的读取操作与相应的缓存
    - 采用队列`_read_buffers`来记录当前发起的IO操作，头部的buffer就是会在`get()`时返回的数据，而后部的buffers就是read ahead操作产生的预读取缓存
    - 当某次`get()`请求不能马上完成（需要等待，头部buffer未就绪时）就会触发`try_increase_read_ahead`来扩大预读取的量
    - 每次`get()`请求都会消费当前的头部buffer并发起新的一次预读取`issue_read_aheads(1)`且并不会等待预读取的完成，形成**类似流水线的处理**，并更新相应的一些指标
    - 从`try_increase_read_ahead`可以看出假如已经有数个未完成的读请求时依然未就绪，则发起的请求就会一直增多直到达到阈值`_options.read_ahead`，而若是启用了动态反馈控制则预读取的数量也会同步调整

    ```cpp
    // seastar/core/fstream.cc
    class file_data_source_impl {
        struct issued_read {
        uint64_t _pos;
        uint64_t _size;
        future<temporary_buffer<char>> _ready;

        issued_read(uint64_t pos, uint64_t size, future<temporary_buffer<char>> f)
            : _pos(pos), _size(size), _ready(std::move(f)) { }
        };
        circular_buffer<issued_read> _read_buffers;
        // ...
    };

    virtual future<temporary_buffer<char>> file_data_source_impl::get() override {
        if (!_read_buffers.empty() && !_read_buffers.front()._ready.available()) {
            try_increase_read_ahead();
        }
        issue_read_aheads(1);
        auto ret = std::move(_read_buffers.front());
        _read_buffers.pop_front();
        update_history_consumed(ret._size);
        _reactor._io_stats.fstream_reads += 1;
        _reactor._io_stats.fstream_read_bytes += ret._size;
        if (!ret._ready.available()) {
            _reactor._io_stats.fstream_reads_blocked += 1;
            _reactor._io_stats.fstream_read_bytes_blocked += ret._size;
        }
        return std::move(ret._ready);
    }

    void file_data_source_impl::try_increase_read_ahead() {
        // Read-ahead can be increased up to user-specified limit if the
        // consumer has to wait for a buffer and we are not in a slow start
        // phase.
        if (_current_read_ahead < _options.read_ahead && !_in_slow_start) {
            _current_read_ahead++;
            if (_options.dynamic_adjustments) {
                auto& h = *_options.dynamic_adjustments;
                h.read_ahead = std::max(h.read_ahead, _current_read_ahead);
            }
        }
    }
    ```

2. **更新IO历史与慢启动**
    流式的文件读入往往是顺序读，但是提供了`skip()`接口因此也可以跳过一些数据，这就可能出现**经常性跳过一些数据，导致原本应该是顺序读的过程实际上是近似随机读**，而近似随机读的场景下维护缓存和预读取就会导致带宽浪费（例如`get(5)->skip(10000)->get(5)...`重复这个模式，在固定缓存大小为10000的情况下就出现了每次读取了10000但只用到了5，有效带宽仅仅只有`5/10000`），因此需要**基于读取模式（access pattern）动态调整缓存区的机制**

    - 在`get()`操作中会返回头部缓存、发起新的预读取、并且更新历史数据`update_history_consumed()`，传入的参数为头部缓存的数据量`ret._size`
    - 当启用动态调整时，就会进一步调用`update_history()`来更新历史数据，（参照了[TCP滑动窗口](https://github.com/JasonYuchen/notes/blob/master/tcpip1/15.TCP_Flow.md#flow-control-and-window-management)的设计？）维护了两个窗口来记录历史数据，分别为`previous_window`和`current_window`，窗口内实际只有两个数据成员`uint64_t window::total_read`和`uint64_t window::unused_read`
    - 当前窗口的大小有上限值`4MiB`，一旦超过就会将当前窗口转变为`previous_window`并创建新的当前窗口
    - 若在慢启动阶段则记录数据并不断修改缓存大小，此时IO请求数据量会迅速升高，具体算法为综合累计两个窗口的数据以及最新传入的数据，通过`below_target()`来判断是否可以安全提升IO请求的数据量，目前设定为`unused`数据量占比必须小于总IO数据量的`1/4`才可以继续提升，避免**过多的`unused`导致大缓存下小带宽利用率**
    - 当没有超过`1/4`设定时就会提升当前缓存大小，同时若更新后的大小超出显式设置缓存大小，就会重新进入慢启动阶段
    - 若在正常运行阶段（非慢启动），仅通过`update_history`记录数据且不会修改缓存

    ```cpp
    void file_data_source_impl::update_history_consumed(uint64_t bytes) {
        if (!_options.dynamic_adjustments) {
            return;
        }
        update_history(0, bytes);
        if (!_in_slow_start) {
            return;
        }
        unsigned new_size = std::min(_current_buffer_size * 2, _options.buffer_size);
        auto& h = *_options.dynamic_adjustments;
        auto total = h.current_window.total_read + h.previous_window.total_read + new_size;
        auto unused = h.current_window.unused_read + h.previous_window.unused_read + new_size;
        // Check whether we can safely increase the buffer size to new_size
        // and still be below unused_ratio_target even if it is entirely
        // dropped.
        if (below_target(unused, total)) {
            _current_buffer_size = new_size;
            _in_slow_start = _current_buffer_size < _options.buffer_size;
        }
    }

    void file_data_source_impl::update_history(uint64_t unused, uint64_t total) {
        // We are maintaining two windows each no larger than window_size.
        // Dynamic adjustment logic uses data from both of them, which
        // essentially means that the actual window size is variable and
        // in the range [window_size, 2*window_size].
        auto& h = *_options.dynamic_adjustments;
        h.current_window.total_read += total;
        h.current_window.unused_read += unused;
        if (h.current_window.total_read >= h.window_size) {
            h.previous_window = h.current_window;
            h.current_window = { };
        }
    }

    static bool file_data_source_impl::below_target(uint64_t unused, uint64_t total) {
        return unused * unused_ratio_target::den < total * unused_ratio_target::num;
        // using unused_ratio_target = std::ratio<25, 100>;
    }
    ```

3. **动态调节**
    - 在慢启动进入常态运行后就确定了缓存大小，但是每次`skip`跳过数据时就调用`update_history_unused`更新`unused`大小，并会判断是否需要重置缓存大小
    - 同样采用`below_target`，假如`skip`后`unused`增加导致不满足`1/4`比例，就会触发重新分配缓存并重新进入慢启动

    ```cpp
    void file_data_source_impl::update_history_unused(uint64_t bytes) {
        if (!_options.dynamic_adjustments) {
            return;
        }
        update_history(bytes, bytes);
        set_new_buffer_size(after_skip::yes);
    }

    void file_data_source_impl::set_new_buffer_size(after_skip skip) {
        if (!_options.dynamic_adjustments) {
            return;
        }
        auto& h = *_options.dynamic_adjustments;
        int64_t total = h.current_window.total_read + h.previous_window.total_read;
        int64_t unused = h.current_window.unused_read + h.previous_window.unused_read;
        if (skip == after_skip::yes && below_target(unused, total)) {
            // Do not attempt to shrink buffer size if we are still below the
            // target. Otherwise, we could get a bad interaction with
            // update_history_consumed() which tries to increase the buffer
            // size as much as possible so that after a single drop we are
            // still below the target.
            return;
        }
        // Calculate the maximum buffer size that would guarantee that we are
        // still below unused_ratio_target even if the subsequent reads are
        // dropped. If it is larger than or equal to the current buffer size do
        // nothing. If it is smaller then we are back in the slow start phase.
        auto new_target = (unused_ratio_target::num * total - unused_ratio_target::den *  unused) / (unused_ratio_target::den - unused_ratio_target::num);
        uint64_t new_size = std::max(new_target, int64_t(minimal_buffer_size()));
        new_size = std::max(uint64_t(1) << log2floor(new_size), uint64_t (minimal_buffer_size()));
        if (new_size >= _current_buffer_size) {
            return;
        }
        _in_slow_start = true;
        _current_read_ahead = std::min(_current_read_ahead, 1u);
        _current_buffer_size = new_size;
    }
    ```

4. **其他**
    `TODO: 初始化、关闭`

## File output stream and write behind

seastar的output file stream**提供了write behind的选项并内建了缓存**从而使得多次写入可以发起并行IO请求，提高写入表现，output file stream并没有像input file stream一样的反馈调节机制，因为output stream总是连续追加的写入模式

### File output stream interface

- 核心接口就是`make_file_output_stream`，并且内建了缓存支持，并总是从0开始追加写入文件，在写入完成关闭前最后才需要调用`flush()`（会重置内部指针的位置，`flush()`后再写入就重新从0开始）
- 在`file_output_stream_options`中可以看出对一个文件输出流来说可以指定缓存大小、write_behind的IO操作数量、调度的优先级（具体分析[见IO Scheduler](https://github.com/JasonYuchen/notes/blob/master/seastar/Disk_IO_Scheduler.md#%E4%BC%98%E5%85%88%E7%BA%A7-priority-classes-in-scylla)）以及预分配大小
- 对于file output stream转换为output stream的过程略`TODO`

```cpp
// seastar/core/fstream.hh
future<output_stream<char>>  make_file_output_stream(
    file file,
    file_output_stream_options options) noexcept;

struct file_output_stream_options {
    // For small files, setting preallocation_size can make it impossible for XFS to find
    // an aligned extent. On the other hand, without it, XFS will divide the file into
    // file_size/buffer_size extents. To avoid fragmentation, we set the defaultbuffer_size
    // to 64k (so each extent will be a minimum of 64k) and preallocation_size to 0 (toavoid
    // extent allocation problems).
    //
    // Large files should increase both buffer_size and preallocation_size.
    unsigned buffer_size = 65536;
    unsigned preallocation_size = 0; ///< Preallocate extents. For large files, set to alarge number (a few megabytes) to reduce fragmentation
    unsigned write_behind = 1; ///< Number of buffers to write in parallel
    ::seastar::io_priority_class io_priority_class = default_priority_class();
};

class data_sink_impl {
public:
    virtual ~data_sink_impl() {}
    virtual temporary_buffer<char> allocate_buffer(size_t size) {
        return temporary_buffer<char>(size);
    }
    virtual future<> put(net::packet data) = 0;
    virtual future<> put(std::vector<temporary_buffer<char>> data) {
        net::packet p;
        p.reserve(data.size());
        for (auto& buf : data) {
            p = net::packet(std::move(p), net::fragment{buf.get_write(), buf.size()},buf.release());
        }
        return put(std::move(p));
    }
    virtual future<> put(temporary_buffer<char> buf) {
        return put(net::packet(net::fragment{buf.get_write(), buf.size()}, buf.release());
    }
    virtual future<> flush() {
        return make_ready_future<>();
    }
    virtual future<> close() = 0;
    // The method should return the maximum buffer size that's acceptable by
    // the sink. It's used when the output stream is constructed without any
    // specific buffer size. In this case the stream accepts this value as its
    // buffer size and doesn't put larger buffers (see trim_to_size).
    virtual size_t buffer_size() const noexcept {
        assert(false && "Data sink must have the buffer_size() method overload");
        return 0;
    }
};
```

### File output stream implementation

1. **基本写入流程**
    - 当未启用`write_behind`时，写入就会直接通过`do_put()`完成，后者内部会进行位置对齐、缓存管理以及`dma_write`操作
    - 当启用`write_behind`时，会通过信号量`semaphore file_data_sink_impl::_write_behind_sem`来控制并发的IO写入请求数量
    - 并发发起多个IO请求时，**可以看出`_background_writes_done`充当了一个类似链表的作用**，每次新的IO请求都通过`when_all`设置该`future`为已经发起的IO请求和当前新IO请求都完成的`future`，从而形成链式关系，并且当中任意一次失败就会设置`_failed`从而阻止后续一切新请求

    ```cpp
    virtual future<> file_data_sink_impl::put(temporary_buffer<char> buf) override {
        uint64_t pos = _pos;
        _pos += buf.size();
        if (!_options.write_behind) {
            return do_put(pos, std::move(buf));
        }
        // Write behind strategy:
        //
        // 1. Issue N writes in parallel, using a semaphore to limit to N
        // 2. Collect results in _background_writes_done, merging exception futures
        // 3. If we've already seen a failure, don't issue more writes.
        return _write_behind_sem.wait().then([this, pos, buf = std::move(buf)] () mutable {
            if (_failed) {
                _write_behind_sem.signal();
                auto ret = std::move(_background_writes_done);
                _background_writes_done = make_ready_future<>();
                return ret;
            }
            auto this_write_done = do_put(pos, std::move(buf)).finally([this] {
                _write_behind_sem.signal();
            });
            _background_writes_done = when_all(std::move(_background_writes_done), std::move(this_write_done))
                    .then([this] (std::tuple<future<>, future<>> possible_errors) {
                // merge the two errors, preferring the first
                auto& e1 = std::get<0>(possible_errors);
                auto& e2 = std::get<1>(possible_errors);
                if (e1.failed()) {
                    e2.ignore_ready_future();
                    return std::move(e1);
                } else {
                    if (e2.failed()) {
                        _failed = true;
                    }
                    return std::move(e2);
                }
            });
            return make_ready_future<>();
        });
    }
    ```

2. **其他**
    `TODO: 初始化、关闭`

### File stream and IO stream

从文件流的接口来看，文件流主要是操纵一个`temporary_buffer<char>`缓存对象（读取或写入），而上从的IO流则是支持任意长度的数据读取或写入，因此将文件流适配到IO流时，IO流的类主要封装了针对缓存的操作，从而能够支持任意长度的数据操作，例如读取一定字节数量的数据：

- `input_stream`主要通过一个缓存作为成员变量`_buf`，进而与底层`data_source`进行处理，而文件流`file_data_source`实现了`data_source`，并且其文件流实现例如`file_data_sink_impl`则实现了`data_sink_impl`，从而完成适配
- `input_stream`需要读取固定字节数量的数据时，首先操作自己的缓存`_buf`，当缓存为空时才会真正向底层发起IO请求

```cpp
template <typename CharType>
class input_stream final {
    static_assert(sizeof(CharType) == 1, "must buffer stream of bytes");
    data_source _fd;
    temporary_buffer<CharType> _buf;
    bool _eof = false;
    // skip
};

template <typename CharType>
future<temporary_buffer<CharType>>
input_stream<CharType>::read_up_to(size_t n) noexcept {
    using tmp_buf = temporary_buffer<CharType>;
    if (_buf.empty()) {
        if (_eof) {
            return make_ready_future<tmp_buf>();
        } else {
            return _fd.get().then([this, n] (tmp_buf buf) {
                _eof = buf.empty();
                _buf = std::move(buf);
                return read_up_to(n);
            });
        }
    } else if (_buf.size() <= n) {
        // easy case: steal buffer, return to caller
        return make_ready_future<tmp_buf>(std::move(_buf));
    } else {
      try {
        // buffer is larger than n, so share its head with a caller
        auto front = _buf.share(0, n);
        _buf.trim_front(n);
        return make_ready_future<tmp_buf>(std::move(front));
      } catch (...) {
        return current_exception_as_future<tmp_buf>();
      }
    }
}
```
