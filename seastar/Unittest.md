# Introduction to Seastar's Unittest

基于Seastar编写的程序中各类函数往往返回值是`seastar::future<T>`，与[协程](https://github.com/JasonYuchen/notes/blob/master/seastar/Coroutines.md)完美融合，因此对函数进行单元测试时，我希望能够有如下的测试代码：

```cpp
TEST(suite, name) {
  auto i = co_await func();
  ASSERT_EQ(i, 0);
}
```

而gtest框架并不支持这样的做法，gtest会根据`TEST(suite, name)`生成一个返回`void`的测试函数，显然不满足协程的使用要求，seastar中提供了自己的测试框架（基于boost）可以做到：

```cpp
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

```cpp
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

```cpp
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

```cpp
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

```cpp
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

## 在Seastar应用中使用GoogleTest

gtest常见的方式就是采用宏`TEST()`定义一个test，或是采用`TEST_F()`定义一个text fixture，显然直接在gtest中采用协程的编程方式会报错，这是因为`TEST_F()`展开后我们编写的测试部分实际上是`void GTEST_TEST_CLASS_NAME_(test_suite_name, test_name)::TestBody()`的函数体：

```cpp
// gtest/internal/gtest-internal.h

#define GTEST_TEST_(test_suite_name, test_name, parent_class, parent_id)       \
  static_assert(sizeof(GTEST_STRINGIFY_(test_suite_name)) > 1,                 \
                "test_suite_name must not be empty");                          \
  static_assert(sizeof(GTEST_STRINGIFY_(test_name)) > 1,                       \
                "test_name must not be empty");                                \
  class GTEST_TEST_CLASS_NAME_(test_suite_name, test_name)                     \
      : public parent_class {                                                  \
   public:                                                                     \
    GTEST_TEST_CLASS_NAME_(test_suite_name, test_name)() = default;            \
    ~GTEST_TEST_CLASS_NAME_(test_suite_name, test_name)() override = default;  \
    GTEST_DISALLOW_COPY_AND_ASSIGN_(GTEST_TEST_CLASS_NAME_(test_suite_name,    \
                                                           test_name));        \
    GTEST_DISALLOW_MOVE_AND_ASSIGN_(GTEST_TEST_CLASS_NAME_(test_suite_name,    \
                                                           test_name));        \
                                                                               \
   private:                                                                    \
    void TestBody() override;                                                  \
    seastar::future<> SeastarBody();                                           \
    static ::testing::TestInfo* const test_info_ GTEST_ATTRIBUTE_UNUSED_;      \
  };                                                                           \
                                                                               \
  ::testing::TestInfo* const GTEST_TEST_CLASS_NAME_(test_suite_name,           \
                                                    test_name)::test_info_ =   \
      ::testing::internal::MakeAndRegisterTestInfo(                            \
          #test_suite_name, #test_name, nullptr, nullptr,                      \
          ::testing::internal::CodeLocation(__FILE__, __LINE__), (parent_id),  \
          ::testing::internal::SuiteApiResolver<                               \
              parent_class>::GetSetUpCaseOrSuite(__FILE__, __LINE__),          \
          ::testing::internal::SuiteApiResolver<                               \
              parent_class>::GetTearDownCaseOrSuite(__FILE__, __LINE__),       \
          new ::testing::internal::TestFactoryImpl<GTEST_TEST_CLASS_NAME_(     \
              test_suite_name, test_name)>);                                   \
  void GTEST_TEST_CLASS_NAME_(test_suite_name, test_name)::TestBody()
```

由于**Seastar所有的任务都是由其底层的reactor引擎来完成的**，因此单个测试也应该如此，从而我们希望每个gtest的test case实际上就是将定义的任务提交给Seastar并同步等待结果，Seastar为了方便测试提供了这种用法，从而允许外界线程能够等待Seastar执行完成某些任务：

```cpp
// outside of Seastar, a.k.a. alien thread
std::future<> fut = seastar::alien::submit_to(
    *seastar::alien::internal::default_instance,  // or use app_template to fetch an alien::instance
    0, // shard id
    some_function); // some function you want Seastar to process
fut.wait();
```

在不修改gtest源码的基础上，我们可以参照`GTEST_TEST_()`宏来定义一个类似的宏，但是在执行测试时实际上是将任务提交给Seastar并等待，修改如下：

- 关键点在于新增加的`seastar::future<> SeastarBody()`，在这个函数内我们就可以使用协程
- 在gtest的测试线程内，原先`TestBody()`是完成一个测试任务，现在只是将我们定义的`SeastarBody()`提交给Seastar运行并等待结果，真正的测试逻辑都在后者中
- 通过自定义宏来实现gtest测试Seastar中的程序

```cpp
#define MY_TEST_(test_suite_name, test_name, parent_class, parent_id)          \
  static_assert(sizeof(GTEST_STRINGIFY_(test_suite_name)) > 1,                 \
                "test_suite_name must not be empty");                          \
  static_assert(sizeof(GTEST_STRINGIFY_(test_name)) > 1,                       \
                "test_name must not be empty");                                \
  class GTEST_TEST_CLASS_NAME_(test_suite_name, test_name)                     \
      : public parent_class {                                                  \
   public:                                                                     \
    GTEST_TEST_CLASS_NAME_(test_suite_name, test_name)() = default;            \
    ~GTEST_TEST_CLASS_NAME_(test_suite_name, test_name)() override = default;  \
    GTEST_DISALLOW_COPY_AND_ASSIGN_(GTEST_TEST_CLASS_NAME_(test_suite_name,    \
                                                           test_name));        \
    GTEST_DISALLOW_MOVE_AND_ASSIGN_(GTEST_TEST_CLASS_NAME_(test_suite_name,    \
                                                           test_name));        \
                                                                               \
   private:                                                                    \
    void TestBody() override;                                                  \
    seastar::future<> SeastarBody();                                           \
    static ::testing::TestInfo* const test_info_ GTEST_ATTRIBUTE_UNUSED_;      \
  };                                                                           \
                                                                               \
  ::testing::TestInfo* const GTEST_TEST_CLASS_NAME_(test_suite_name,           \
                                                    test_name)::test_info_ =   \
      ::testing::internal::MakeAndRegisterTestInfo(                            \
          #test_suite_name, #test_name, nullptr, nullptr,                      \
          ::testing::internal::CodeLocation(__FILE__, __LINE__), (parent_id),  \
          ::testing::internal::SuiteApiResolver<                               \
              parent_class>::GetSetUpCaseOrSuite(__FILE__, __LINE__),          \
          ::testing::internal::SuiteApiResolver<                               \
              parent_class>::GetTearDownCaseOrSuite(__FILE__, __LINE__),       \
          new ::testing::internal::TestFactoryImpl<GTEST_TEST_CLASS_NAME_(     \
              test_suite_name, test_name)>);                                   \
  void GTEST_TEST_CLASS_NAME_(test_suite_name, test_name)::TestBody() {        \
    auto fut = seastar::alien::submit_to(                                      \
        *seastar::alien::internal::default_instance,                           \
        0,                                                                     \
        [this] { return this->SeastarBody(); });                               \
    fut.wait();                                                                \
  }                                                                            \
  seastar::future<>                                                            \
  GTEST_TEST_CLASS_NAME_(test_suite_name, test_name)::SeastarBody()

#define MY_TEST_F(test_fixture, test_name)                                     \
  MY_TEST_(test_fixture, test_name, test_fixture,                              \
               ::testing::internal::GetTypeId<test_fixture>())
```

由于Seastar还**需要显式构建一个reactor引擎才能执行提交的测试任务**，因此我们将构建引擎的部分放在继承自gtest `Environment`类中，gtest会负责在运行所有测试前首先运行该环境类：

- `SetUp()`中我们启动Seastar，并接管信号处理，通过`std::future`来协调主线程等待reactor引擎完全启动后才会退出进入的测试
- `TearDown()`中我们向Seastar发送信号来停止执行，并等待Seastar完全退出，注意也通过`std::future`协调主线程等待reactor引擎的每个线程退出
- 额外添加的`static void submit(std::function<seastar::future<>()> func)`用于方便的在默认shard上执行协程逻辑，可以使用在其他test suite的`SetUp/TearDown`中

```cpp
class base : public ::testing::Environment {
 public:
  base(int argc, char** argv) : _argc(argc), _argv(argv) {}

  void SetUp() override {
    app_template::config app_cfg;
    app_cfg.auto_handle_sigint_sigterm = false;
    _app = std::make_unique<app_template>(std::move(app_cfg));
    std::promise<void> pr;
    auto fut = pr.get_future();
    _engine_thread = std::thread([this, pr = std::move(pr)]() mutable {
      return _app->run(
          _argc, _argv,
          // We cannot use `pr = std::move(pr)` here as it will forbid compilation
          // see https://taylorconor.com/blog/noncopyable-lambdas/
          [&pr]() mutable -> seastar::future<> {
            l.info("reactor engine starting...");
            rafter::util::stop_signal stop_signal;
            pr.set_value();
            auto signum = co_await stop_signal.wait();
            l.info("reactor engine exiting..., caught signal {}:{}",
                  signum, ::strsignal(signum));
            co_return;
          });
    });
    fut.wait();
  }

  void TearDown() override {
    vector<std::future<void>> futs;
    for (auto shard = 0; shard < smp::count; ++shard) {
      futs.emplace_back(
          alien::submit_to(
              *alien::internal::default_instance,
              shard,
              [] { return make_ready_future<>(); }));
    }
    for (auto&& fut : futs) {
      fut.wait();
    }
    auto ret = ::pthread_kill(_engine_thread.native_handle(), SIGTERM);
    if (ret) {
      l.error("send SIGTERM failed: {}", ret);
      std::abort();
    }
    _engine_thread.join();
  }

  static void submit(std::function<seastar::future<>()> func) {
    seastar::alien::submit_to(
      *seastar::alien::internal::default_instance, 0, std::move(func)).wait();
  }

 protected:
  int _argc;
  char** _argv;
  std::thread _engine_thread;
  std::unique_ptr<seastar::app_template> _app;
};

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  // 注册初始化Seastar的环境类test::base到gtest中
  ::testing::AddGlobalTestEnvironment(new test::base(argc, argv));
  return RUN_ALL_TESTS();
}
```

通过这种方式，我们就可以采用与gtest测试其他框架完全相同的方式来测试Seastar的函数，例如：

```cpp
class component : public my_test_base {};

MY_TEST_F(component, basic) {
  EXPECT_EQ(1, 1);
  co_await some_function;
}
```

由于gtest中的`ASSERT_*`系列函数在失败时就会通过`return`返回，这与协程中的`co_return`违背，可以发现这些函数实际实现如下：

```cpp
// gtest/gtest.h
#define ASSERT_TRUE(condition) \
  GTEST_TEST_BOOLEAN_(condition, #condition, false, true, \
                      GTEST_FATAL_FAILURE_)

// gtest/internal/gtest-internal.h
#define GTEST_TEST_BOOLEAN_(expression, text, actual, expected, fail) \
  GTEST_AMBIGUOUS_ELSE_BLOCKER_ \
  if (const ::testing::AssertionResult gtest_ar_ = \
      ::testing::AssertionResult(expression)) \
    ; \
  else \
    fail(::testing::internal::GetBoolAssertionFailureMessage(\
        gtest_ar_, text, #actual, #expected).c_str())
```

其中所有`ASSERT_*`系列函数失败时依赖了`fail`参数即`GTEST_FATAL_FAILURE_`宏，而该宏就是简单的打印消息并返回，将此处的`return`替换为`co_return`就可以实现在协程函数体内执行`ASSERT_*`：

```cpp
// gtest/internal/gtest-internal.h
#define GTEST_FATAL_FAILURE_(message) \
  return GTEST_MESSAGE_(message, ::testing::TestPartResult::kFatalFailure)
```

为了避免直接修改gtest的源代码，依然选择在前文定义`MY_TEST_`的位置覆盖这个宏的定义，从而我们的测试函数体内就可以使用`ASSERT_*`：

```cpp
#ifdef GTEST_FATAL_FAILURE_
#undef GTEST_FATAL_FAILURE_
#endif

#define GTEST_FATAL_FAILURE_(message)                                          \
  co_return GTEST_MESSAGE_(message, ::testing::TestPartResult::kFatalFailure)

#ifdef GTEST_SKIP_
#undef GTEST_SKIP_
#endif

#define GTEST_SKIP_(message)                                                   \
  co_return GTEST_MESSAGE_(message, ::testing::TestPartResult::kSkip)
```

## 注意点与局限

1. 当实现一个test suite时会添加一些成员变量，这些成员变量的初始化可以通过`SetUp`中使用`submit`由Seastar来完成，但是需要注意**由Seastar线程创建的数据应该由相应的Seastar线程来销毁**，例如：

    ```cpp
    class segment_test : public ::testing::Test {
     protected:
      void SetUp() override {
        // let the Seastar initialize the object,
        // this call blocks until _segment is set
        submit([this]() -> future<> {
          _segment = co_await segment::open(test_file);
        });
      }

      void TearDown() override {
        submit([this]() -> future<> {
          co_await _segment->close();
          // data created by Seastar must also be released inside the Seastar
          // (in the same thread/shard)
          _segment.reset(nullptr);
          co_await remove_file(test_file);
        });
      }
      seastar::lw_shared_ptr<segment> _segment;
    };
    ```
