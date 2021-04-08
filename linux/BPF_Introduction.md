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

`TODO`

## 3. 性能分析 Performance Analysis

`TODO`

## 4. BCC

`TODO`

## 5. bpftrace

`TODO`
