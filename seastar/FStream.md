# seastar的文件流

`TODO`

## File input stream and read ahead

seastar支持DMA方式的磁盘IO，即`O_DIRECT`，则在读取文件数据时会绕过OS的page cache，那么当需要连续读取时往往性能就会弱于有page cache的方式，[参考此处](https://github.com/JasonYuchen/notes/blob/master/linux/io_uring.md#modern-storage-is-plenty-fast-it-is-the-apis-that-are-bad)，seastar的input file stream**提供了read ahead的选项并内建了缓存**从而能够达到更高的连续读取表现，并且在此之上，seastar提供了动态反馈调节，根据访问模式对read ahead的行为进行动态调整，深入优化了IO的表现

这一节从源码的角度来看seastar的做法，其input stream的实现、内建缓存与读取时的行为、动态调整的思路：

1. **接口 Interface**
   - 核心接口就是`make_file_input_stream`，并且由于内建了缓存支持，这里的`offset`不需要对齐
   - 在`file_input_stream_options`中可以看出对一个文件流来说可以指定缓存大小、read_ahead的IO操作数量、调度的优先级（具体分析[见IO Scheduler](https://github.com/JasonYuchen/notes/blob/master/seastar/Disk_IO_Scheduler.md#%E4%BC%98%E5%85%88%E7%BA%A7-priority-classes-in-scylla)）以及动态调整选项
   - 对于动态调整选项而言，初始可以采用默认空历史也可以显式给定历史IO情况以更好的指导input stream的read ahead行为
   - 对于file input stream转换为input stream的过程略`TODO`

    ```C++
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

2. **实现 Implementation**
   **基本读取流程**
   - `struct issued_read`保存了单次真正的读取操作与相应的缓存
   - 采用队列`_read_buffers`来记录当前发起的IO操作，头部的buffer就是会在`get()`时返回的数据，而后部的buffers就是read ahead操作产生的预读取缓存
   - 当某次`get()`请求不能马上完成（需要等待，头部buffer未就绪时）就会触发`try_increase_read_ahead`来扩大预读取的量
   - 每次`get()`请求都会消费当前的头部buffer并发起新的一次预读取`issue_read_aheads(1)`且并不会等待预读取的完成，形成**类似流水线的处理**，并更新相应的一些指标
   - 从`try_increase_read_ahead`可以看出假如已经有数个未完成的读请求时依然未就绪，则发起的请求就会一直增多直到达到阈值`_options.read_ahead`，而若是启用了动态反馈控制则预读取的数量也会同步调整

    ```C++
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

    **更新IO历史与反馈调节**


    ```C++
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
    }
    ```
