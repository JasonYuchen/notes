# BPF Performance Tools

[BPF Performance Tools: Linux System and Application Observability](http://www.brendangregg.com/bpf-performance-tools-book.html) Part I: Technologies

## 1. 简介 Introduction

### 术语

- **Tracing 跟踪**：基于事件的记录event-based recording，BPF工具也基于此，此类tracing工具例如Linux下的`strace`记录了系统调用事件
- **Snooping 嗅探**：tracing、snooping、event dumping通常都指基于事件的记录
- **Sampling 采样**：对程序运行进行采样，使用采样结果来描述整个运行情况，也被称为性能剖析profiling，显然定时采样的性能代价要比tracing要小，前者使用采样结果来描述全局，而后者记录了每个事件，但缺点就是采样有可能失真
- **Profiling 剖析**：通常就是指创建采样进行性能剖析
- **Observability 可观测性**：指采用一系列不同种类的工具对系统进行多个方面的分析和理解，注意可观测性并不包括性能测试benchmark，性能测试会模拟工作负载从而改变了系统的运行状态，**可观测性旨在分析和理解系统，性能测试旨在模拟不同工况下系统的响应**

### BCC，bpftrace，IO Visor的关系

BPF非常底层，直接采用BPF编程极其艰深，因此诞生了多种前端辅助工具

![bpf1](images/bpf1.png)

- **BCC, BPF Compiler Collection**：提供了多种语言内的BPF框架，并且[BCC repo](https://github.com/iovisor/bcc)已经提供了多种BPF工具可以直接用于性能分析和问题追踪
- **bpftrace**：新的BPF前端辅助工具，地位与BCC类似，用于构建新的BPF工具
- **IO Visor**：BCC和bpftrace隶属于[IO Visor项目](https://github.com/iovisor)，而IO Visor属于Linux Foundation

### 实例`biolatency`

使用BPF提供的`biolatency`工具可以观测block I/O的请求响应延迟，如下：

```text
# biolatency -m
Tracing block device I/O... Hit Ctrl-C to end.
^C
 msecs              : count      distribution
     0 -> 1         : 16335     |****************************************|
     2 -> 3         : 2272      |*****                                   |
     4 -> 7         : 3603      |********                                |
     8 -> 15        : 4328      |**********                              |
    16 -> 31        : 3379      |********                                |
    32 -> 63        : 5815      |**************                          |
    64 -> 127       : 0         |                                        |
   128 -> 255       : 0         |                                        |
   256 -> 511       : 0         |                                        |
   512 -> 1023      : 11        |                                        |
```

### BPF跟踪的可见性

在生产环境中可以直接使用BPF提供的工具而**不需要重启服务器或是重启应用**

![bpf tools](images/bpf_performance_tools_book.png)

### 动态跟踪：kprobes和uprobes

在应用运行过程中，**动态插入追踪点探测运行状况**，而在没有跟踪时不会造成任何性能损失

- **kprobes**：dynamic instrumentation for kernel functions
- **uprobes**：dynamic instrumentation for user functions

|Probe example|Description|
|:-|:-|
|`kprobe:vfs_read`|追踪点为内核`vfs_read()`函数开始|
|`kretprobe:vfs_read`|追踪点为内核`vfs_read()`函数结束|
|`uprobe:/bin/bash:readline`|追踪点为`/bin/bash`程序的`readline()`函数开始|
|`uretprobe:/bin/bash:readline`|追踪点为`/bin/bash`程序的`readline()`函数结束|

### 静态跟踪：Tracepoints和USDT

**动态跟踪依赖于运行时的函数**，那么由于应用程序内的函数可能随着版本变化而改变，此时已经完成的动态跟踪脚本可能会不适用，例如追踪的函数在新版本中已经被移除等，同时编译应用时的函数内联inline等也可能会导致动态跟踪不可用，而**静态跟踪通过由应用开发者自行在源码中维护追踪点**，从而避免这些问题

- **Tracepoints**：kernel static instrumentation
- **USDT, User-level Statically Defined Tracing**：user-level static instrumentation

|Probe example|Description|
|:-|:-|
|`tracepoint:syscalls:sys_enter_open`|追踪点位于内核`open()`系统调用|
|`usdt:/usr/sbin/mysqld:mysql:query__start`|追踪点位于`/usr/sbin/mysqld`中的`query_start`函数|

使用bpftrace静态追踪`open()`系统调用的示例如下，输出了调用`open`的进程名以及传给`open`的参数`filename`：

```text
# bpftrace -e 'tracepoint:syscalls:sys_enter_open { printf("%s %s\n", comm, str(args->filename)); }'
Attaching 1 probe...
slack /run/user/1000/gdm/Xauthority
slack /run/user/1000/gdm/Xauthority
^C
```

## 2. 技术背景 Technology Background

### Extended BPF, eBPF

历史与底层实现技术暂时跳过

### 调用栈回溯

1. **基于栈帧指针的栈 Frame Pointer-Based Stacks**
  通常栈帧都可以通过链表的方式连接起来，从而可以通过`%RBP`访问最顶层的栈，随后基于固定的偏移值（+8）来逐层获取所有栈——**调用栈回溯stack walking**
  注意：AMD64 ABI指出使用`%RBP`是冗余的，因此通常GCC等编译器会默认忽略栈帧指针，而将`%RBP`用作通用寄存器，通过编译时`-fno-omit-frame-pointer`可以显式要求使用`%RBP`
  ![bpf2](images/bpf2.png)
2. **符号Symbols**
   当前内核仅记录栈回溯时的地址，随后在用户空间将地址转换成对应的符号（例如函数名等），因此有可能会出现记录时的地址与翻译时符号对应的地址不一致的情况
   未来有可能在内核直接进行地址到符号的转换

### 火焰图

[火焰图flame graph](http://www.brendangregg.com/flamegraphs.html)是最重要的性能数据可视化方式之一

假定进行定时对栈采样10次，结果如下：

```text
   1          2          7
func_e              
func_d                func_c
func_b     func_b     func_b
func_a     func_a     func_a
```

则可以转换成这样的火焰图：

![bpf3](images/bpf3.png)

- **每一个方块代表栈中的一个函数**（即一个stack frame）
- **Y轴**：从底层往上就是调用栈从底部到顶部，即**函数的调用关系**
- **X轴**：宽度代表每个**函数出现在每次采样中的比例**，注意这不代表时间上从左到右是调用的先后，图中`func_c`在`func_d`前面并不是先调用`func_c`再调用`func_d`，**从左到右的函数排序只是字母顺序**

**每个函数顶部未被其他函数覆盖的长度就是每个函数自身的性能开销，越宽越有可能是性能瓶颈（the widest towers）**，例如：

- `func_a`几乎被`func_b`完全覆盖近似没有开销
- `func_b`在图中左侧大约有20%的耗时
- `func_c`在图中中间大约有70%的耗时
- `func_d`几乎被`func_e`完全覆盖近似没有开销
- `func_e`在图中右侧顶部大约有10%的耗时

通常火焰图还会支持一些高级特性如下：

- **颜色 color palettes**：不同色调hue代表函数种类（用户、内核、库等等），不同饱和度saturation代表不同函数名（相同函数名的饱和度相同以区分不同的函数），背景色background表示火焰图种类帮助识别（蓝色代表IO、红色代表CPU等）
- **提示 mouse-overs**：当鼠标掠过某个函数时展示额外的提示
- **缩放 zoom**：可以选择某个函数名作为最底层的函数，只显式该函数上方的所有调用关系与耗时
- **搜索 search**

### kprobes

kprobes提供了Linux内核的动态探测功能，能够**对生产环境下的内核进行实时探测**而不需要重启系统或是以特殊模式运行内核，另外可以使用kretporbes对函数返回进行探测，从而**基于kprobes和kretprobes的时间差就可以直接探测函数的运行时间**

1. **kprobes接口**
   - kprobe API：例如`register_kprobe()`等
   - `/sys/kernel/debug/tracing/kprobe_events`：通过对该文件写入配置字符串来控制kprobe
   - `perf_event_open()`：实际上已经在`perf`工具中使用
2. **BPF和kprobes**
   - BCC：提供了`attach_kprobe()`和`attach_kretprobe()`
   - bpftrace：提供了`kprobe`和`kretprobe`类型

   例如BCC中提供了`vfsstat`工具来探测对VFS的调用情况，其实现就是利用了`attach_kprobe()`：

   ```text
   # vfsstat
   TIME READ/s WRITE/s CREATE/s OPEN/s FSYNC/s
   07:48:16: 736 4209 0 24 0
   07:48:17: 386 3141 0 14 0
   07:48:18: 308 3394 0 34 0
   07:48:19: 196 3293 0 13 0
   07:48:20: 1030 4314 0 17 0
   07:48:21: 316 3317 0 98 0
   [...]

   # grep attach_ vfsstat.py
   b.attach_kprobe(event="vfs_read", fn_name="do_read")
   b.attach_kprobe(event="vfs_write", fn_name="do_write")
   b.attach_kprobe(event="vfs_fsync", fn_name="do_fsync")
   b.attach_kprobe(event="vfs_open", fn_name="do_open")
   b.attach_kprobe(event="vfs_create", fn_name="do_create")
   ```

### uprobes

uprobes与kprobes类似，提供了**用户态函数的动态探测功能**，另外可以使用uretprobes对函数返回进行探测

1. **uprobes接口**
   - `/sys/kernel/debug/tracing/uprobe_events`：通过对该文件写入配置字符串来控制uprobe
   - `perf_event_open()`：实际上已经在`perf`工具中使用
2. **BPF和uprobes**
   - BCC：提供了`attach_uprobe()`和`attach_uretprobe()`
   - bpftrace：提供了`uprobe`和`uretprobe`类型

   例如BCC中提供了`gethostlatency`工具来探测DNS解析延迟（基于对`getaddrinfo/gethostbyname`探测），其实现就是利用了`attach_uprobe()`和`attach_uretprobe()`的时间差计算延迟：

   ```text
   # gethostlatency
   TIME     PID   COMM            LATms HOST
   01:42:15 19488 curl            15.90 www.brendangregg.com
   01:42:37 19476 curl            17.40 www.netflix.com
   01:42:40 19481 curl            19.38 www.netflix.com
   01:42:46 10111 DNS Res~er #659 28.70 www.google.com

   # grep attach_ gethostlatency.py
   b.attach_uprobe(name="c", sym="getaddrinfo", fn_name="do_entry", pid=args.pid)
   b.attach_uprobe(name="c", sym="gethostbyname", fn_name="do_entry",
   b.attach_uprobe(name="c", sym="gethostbyname2", fn_name="do_entry",
   b.attach_uretprobe(name="c", sym="getaddrinfo", fn_name="do_return",
   b.attach_uretprobe(name="c", sym="gethostbyname", fn_name="do_return",
   b.attach_uretprobe(name="c", sym="gethostbyname2", fn_name="do_return"
   ```

注意：uprobes可以探测一些调用极其频繁的操作，例如`malloc()/free()`，但也**会带来额外的性能开销**，极端情况下可能导致应用程序性能下降十倍，一些其他探测用户态函数的功能（LTTng-UST等）正在被探讨和研究

### Tracepoints

Tracepoints用于kernel的静态探测，需要开发者手动在内核函数中嵌入检查点，因此相对更为复杂与繁琐，与kprobes相比：

|Detail|krpobes|Tracepoints|
|:-|:-|:-|
|Type|Dynamic|Static|
|Rough # of events|50k+|100+|
|Kernel maintenance|None|Required|
|Disabled overhead|None|Tiny|
|Stable API|No|Yes|

由于Tracepoints提供稳定的API，因此通常内核升级后也可以继续使用原API，**尝试首先使用Tracepoints，功能不足时再考虑kprobes**

1. **Tracepoint接口**
   - Ftrace-based，通过`/sys/kernel/debug/tracing/events`下的一些列目录中文件进行开启和关闭，每个文件对应不同的tracepoint
   - `perf_event_open()`：实际上已经在`perf`工具中使用
2. **Tracepoint和BPF**
   - BCC：提供了`TRACEPOINT_PROBE()`
   - bpftrace：提供了`tracepoint probe`类型

   例如BCC中提供了`tcplife`工具来探测TCP会话的细节：

   ```text
   # tcplife
   PID   COMM       LADDR        LPORT RADDR        RPORT  TX_KB  RX_KB    MS
   22597 recordProg 127.0.0.1    46644 127.0.0.1    28527    0      0     0.23
   3277  redis-serv 127.0.0.1    28527 127.0.0.1    46644    0      0     0.28
   22598 curl       100.66.3.172 61620 52.205.89.26 80       0      1     91.79
   ```

### USDT

User-level Statically Defined Tracing, USDT用于user space的静态探测，而用在应用程序中的静态探测实际上非常多，很多库/项目都有自用的一整套基础设施用于探测事件

1. **USDT和BPF**
   - BCC：提供了`USDT().enable_probe()`
   - bpftrace：提供了`usdt probe`类型

### 性能监测 PMCs

性能监测计数器Performance Monitoring Counters, PMCs（也被称为Performance Instrumentation Counters, PICs或CPU Performance Counters, CPCs）是一组**CPU硬件级别的可编程计数器用于探测CPU性能表现**，以Intel为例：

|Event Name|UMask|Event Select|
|:-|:-|:-|
|UnHalted Core Cycles|00H|3CH|
|Instruction Retired|00H|C0H|
|UnHalted Reference Cycles|01H|3CH|
|LLC References|4FH|2EH|
|LLC Misses|41H|2EH|
|Branch Instruction Retired|00H|C4H|
|Branch Misses Retired|00H|C5H|

- **Counting Mode**：此模式下PMCs追踪事件的发生率，kernel可以直接从计数器中读取相应的值，额外开销近似于0
- **Overflow Sampling Mode**：此模式下PMCs可以发送中断给kernel，从而kernel就可以收集各种状态，但是需要特别注意中断的频率，探测某些操作可能会导致每秒数百万次中断导致近似停机，通常是采用计数器达到阈值时才发送一次中断的方式（例如每10000LLC缓存未命中就发送一次中断）

## 3. 性能分析 Performance Analysis

### 目标 Goals

- **延迟 Latency**
- **频率 Rate**
- **吞吐量 Throughput**
- **利用率 Utilization**
- **代价 Cost**

### 性能测试的方法 Performance Methodologies

- **工作负载特征 Workload Characterization**：首先需要描述工作负载的特征，才可以为后续的分析做准备，不同的工作负载会影响程序不同的方面（CPU密集型、I/O密集型等）
- **逐步深入分析 Drill-Down Analysis**：从一个指标值入手，将其分解为一系列构成这个值的部分，随后继续选择影响最大的部分继续分解，直到找到根本原因root cause
- **USE方法**：对每个资源需要检查Utilization、Saturation、Errors，首先需要画出整个应用程序涉及到的所有硬软件资源，然后对每个资源检查USE

### Linux 60秒分析方法

对于任一台性能存在问题的Linux服务器，都可以执行下列命令，在60秒内就可以分析出很多问题

1. `uptime`：快速查看**系统负载**情况，系统负载表示为当前希望得到运行的任务/进程数量，三个数字分别是1-min/5-min/15-min的指数衰减值
2. `dmesg | tail`：默认显示最近的10条系统消息，可能会有`Out of memory: Kill...`、`TCP: Possible SYN flooding...`等**性能相关的日志**
3. `vmstat 1`：展示**虚拟内存的统计信息**，参数`1`代表展示一秒内的统计和，第一行代表系统自启动以来的统计和，每一列的含义如下
   - `r`：当前正在CPU上运行的进程数量，因此当数值超过实际CPU核数时代表系统CPU饱和
   - `free`：空闲的内存量
   - `si, so`：交换区换入swap-ins/换出swap-outs的数量，当不为零时说明内存已不足，系统正在使用交换空间
   - `us, sy, id, wa, st`：将CPU时间分解为user，system(kernel)，idle，wait I/O，stolen(by other guests/Xen/etc)
4. `mpstat -P ALL 1`：将**每个CPU核心的时间分解为不同的状态**（usr/sys/etc），例如CPU 0的`%usr`列是100，就说明了有个单线程的程序正在运行并完全占用CPU 0
5. `pidstat 1`：展示**每个进程的CPU使用情况**
6. `iostat -xz 1`：展示存储设备的I/O指标，每一列的含义如下
   - `r/s, w/s, rkB/s, wkB/s`：分别代表该设备的I/O读的频率、写的频率、每秒读取、每秒写入
   - `await`：I/O请求的平均时间，包含了每个请求的排队时间以及处理时间，应用程序对该值很敏感
   - `avgqu-sz`：发送给设备的平均请求数量
   - `%util`：设备的利用率，即设备在工作的时间占总时间比例，超过60%往往就意味着性能下降，接近100%就是饱和
7. `free -m`：展示可用的内存量
8. `sar -n DEV 1`：展示网络设备的指标，发送/接收数据量
9. `sar -n TCP,ETCP 1`：展示TCP的指标，每一列的含义如下
   - `active/s`：每秒本地打开的TCP连接数（`connect()`）
   - `passive/s`：每秒远程打开的TCP连接数（`accept()`）
   - `retrans/s`：每秒的TCP重传次数
10. `top`：展示系统和各进程的汇总信息，可以与上述命令的结果进行对比确认

## 4. BCC

`TODO`

## 5. bpftrace

`TODO`
