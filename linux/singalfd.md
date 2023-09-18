# singalfd

用`signalfd`把信号直接转换为文件描述符事件，从而根本上避免signal handler，并且将`signalfd`融入到IO管理中，但也并不是没有局限：

> signalfd **doesn’t solve any of the other troubles with signal handling**: it just saves you a separate thread.

- [man signalfd(2)](https://man7.org/linux/man-pages/man2/signalfd.2.html)
- [Linux fd 系列 — signalfd 是什么 ？](https://zhuanlan.zhihu.com/p/418256266)
- [make signals less painful](https://unixism.net/2021/02/making-signals-less-painful-under-linux/)
- [Signalfd is useless](https://ldpreload.com/blog/signalfd-is-useless)
