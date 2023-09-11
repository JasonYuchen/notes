# seastar编译与测试

操作系统：Ubuntu 20.04

1. 安装依赖库

    ```shell
    sudo ./install-dependencies.sh
    ```

2. 配置编译选项
   参考configure.py的内容，有多个可控选项，这里选择如下

    ```shell
    ./configure.py --mode=debug --compiler=clang++-12 --c-compiler=clang-12 --c++-dialect=c++20 --prefix=/usr/local --cflags=-fcoroutines-ts --enable-dpdk --disable-hwloc --verbose
    ```

3. 编译与安装

    ```shell
    sudo ninja -C build/debug install
    ```

   - 编译过程中可能出现与`boost::test`相关的错误，[原因见此](https://github.com/boostorg/test/pull/252)，快速修改方式如下：

        ```cpp
        // include/boost/test/impl/test_tools.ipp: line 127
        ostr << (t ? reinterpret_cast<char const*>(t) : "null string" );
        ```

4. 运行测试

    ```shell
    ./test.py --mode debug
    ```

   - seastar依赖Linux AIO来执行异步I/O操作，同时依赖大页内存，因此运行前还需要修改`fs.aio-max-nr`以及`vm.nr_hugepages`
   - 如果使用`g++ 10.2.0`编译，则运行时会出现AddressSanitizer导致的SegFault，这并不是代码错误，而是由于目前AddressSanitizer不支持coroutine导致的，[见此](https://gcc.gnu.org/bugzilla/show_bug.cgi?id=95137)，因此暂时使用`clang 12`
   - 如果默认使用了`hwloc`库，其使用的libltdl有静态变量导致的内存泄漏，可以忽略，也可以显式`--disable-hwloc`来避免
   - tls相关的测试会超时，查看源码发现是由于测试目标是`www.google.com`所致
