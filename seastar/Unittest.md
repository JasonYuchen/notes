# Introduction to Seastar's Unittest

基于Seastar编写的程序中各类函数往往返回值是`seastar::future<T>`，与[协程](https://github.com/JasonYuchen/notes/blob/master/seastar/Coroutines.md)完美融合，因此对函数进行单元测试时，我希望能够有如下的测试代码：

```C++
TEST(suite, name) {
  auto i = co_await func();
  ASSERT_EQ(i, 0);
}
```

而gtest框架并不支持这样的做法，gtest会根据`TEST(suite, name)`生成一个返回`void`的测试函数，显然不满足协程的使用要求，seastar中提供了自己的测试框架（基于boost）可以做到：

```C++
// seastar/tests/unit/coroutines_test.cc
SEASTAR_TEST_CASE(test_preemption) {
    bool x = false;
    unsigned preempted = 0;
    auto f = later().then([&x] {
            x = true;
        });

    // try to preempt 1000 times. 1 should be enough if not for
    // task queue shaffling in debug mode which may cause co-routine
    // continuation to run first.
    while(preempted < 1000 && !x) {
        preempted += need_preempt(); 
        co_await make_ready_future<>();
    }
    auto save_x = x;
    // wait for later() to complete
    co_await std::move(f);
    BOOST_REQUIRE(save_x);
    co_return;
}
```

seastar提供的测试宏`SEASTAR_TEST_CASE`主要展开为一个结构体（与gtest相同）并且其函数体主要位于`run_test_case`中，相应的返回值为`seastar::future<>`从而能够使用协程的方式来编写测试代码

```C++
// seastar/testing/test_case.hh
#define SEASTAR_TEST_CASE(name) \
    struct name : public seastar::testing::seastar_test { \
        const char* get_test_file() override { return __FILE__; } \
        const char* get_name() override { return #name; } \
        seastar::future<> run_test_case() override; \
    }; \
    static name name ## _instance; \
    seastar::future<> name::run_test_case()

```

进一步来看seastar的测试运行的实现，主要是每个测试类所继承的`seastar::testing::seastar_test`，其构造函数中完成了测试的注册：

```C++
// seastar/testing/seastar_test.hh
class seastar_test {
public:
    seastar_test();
    virtual ~seastar_test() {}
    virtual const char* get_test_file() = 0;
    virtual const char* get_name() = 0;
    virtual int get_expected_failures() { return 0; }
    virtual future<> run_test_case() = 0;
    void run();
};


// seastar/testing/seastar_test.cc

// We store a pointer because tests are registered from dynamic initializers,
// so we must ensure that 'tests' is initialized before any dynamic initializer.
// I use a primitive type, which is guaranteed to be initialized before any
// dynamic initializer and lazily allocate the factor.

static std::vector<seastar_test*>* tests = nullptr;

const std::vector<seastar_test*>& known_tests() {
    if (!tests) {
        throw std::runtime_error("No tests registered");
    }
    return *tests;
}

seastar_test::seastar_test() {
    if (!tests) {
        tests = new std::vector<seastar_test*>();
    }
    tests->push_back(this);
}
```

在`seastar_test::run()`中调用了每个测试的`run_test_case()`来实际运行测试：

```C++
// seastar/testing/seastar_test.cc
void seastar_test::run() {
    // HACK: please see https://github.com/cloudius-systems/seastar/issues/10
    BOOST_REQUIRE(true);

    // HACK: please see https://github.com/cloudius-systems/seastar/issues/10
    boost::program_options::variables_map()["dummy"];

    set_abort_on_internal_error(true);

    global_test_runner().run_sync([this] {
        return run_test_case();
    });
}
```

而所有注册的测试通过`known_tests()`暴露给`test_runner`，由后者逐个调用`seastar_test::run()`完成测试：

```C++
// seastar/testing/test_runner.cc
void test_runner::run_sync(std::function<future<>()> task) {
    exchanger<std::exception_ptr> e;
    _task.give([task = std::move(task), &e] {
        try {
            return task().then_wrapped([&e](auto&& f) {
                try {
                    f.get();
                    e.give({});
                } catch (...) {
                    e.give(std::current_exception());
                }
            });
        } catch (...) {
            e.give(std::current_exception());
            return make_ready_future<>();
        }
    });
    auto maybe_exception = e.take();
    if (maybe_exception) {
        std::rethrow_exception(maybe_exception);
    }
}
```

`TODO: 魔改gtest来支持seastar?`
