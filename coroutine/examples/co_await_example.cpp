// Copyright (c) Lewis Baker
// 
// require g++ version 10.2
// 
// g++ -std=c++20 -fcoroutines co_await_example.cpp
// ./a.out

#include <atomic>
#include <coroutine>
#include <iostream>
#include <thread>
#include <chrono>

class async_manual_reset_event
{
public:

  // 初始状态可以是set或者是not set
  async_manual_reset_event(bool initiallySet = false) noexcept
   : m_state(initiallySet ? this : nullptr)
  {
  }

  // No copying/moving
  async_manual_reset_event(const async_manual_reset_event&) = delete;
  async_manual_reset_event(async_manual_reset_event&&) = delete;
  async_manual_reset_event& operator=(const async_manual_reset_event&) = delete;
  async_manual_reset_event& operator=(async_manual_reset_event&&) = delete;

  bool is_set() const noexcept
  {
    return m_state.load(std::memory_order_acquire) == this;
  }

  struct awaiter;
  awaiter operator co_await() const noexcept;

  void set() noexcept;
  void reset() noexcept
  {
    void *oldValue = this;
    m_state.compare_exchange_strong(oldValue, nullptr, std::memory_order_acquire);
  }

private:

  friend struct awaiter;

  // - 'this' => set state
  // - otherwise => not set, head of linked list of awaiter*.
  mutable std::atomic<void*> m_state;
};

struct async_manual_reset_event::awaiter
{
  awaiter(const async_manual_reset_event& event) noexcept
  : m_event(event)
  {}

  // 如果已经set就不需要再等待，可以直接继续执行
  bool await_ready() const noexcept
  {
    return m_event.is_set();
  }

  bool await_suspend(std::coroutine_handle<> awaitingCoroutine) noexcept
  {
    // Special m_state value that indicates the event is in the 'set' state.
    const void* const setState = &m_event;

    // Remember the handle of the awaiting coroutine.
    m_awaitingCoroutine = awaitingCoroutine;

    // Try to atomically push this awaiter onto the front of the list.
    // 使用acquire是为了确保set()前的写入在这里可见
    void* oldValue = m_event.m_state.load(std::memory_order_acquire);
    do {
      // Resume immediately if already in 'set' state.
      if (oldValue == setState) return false; 

      // Update linked list to point at current head.
      m_next = static_cast<awaiter*>(oldValue);

      // Finally, try to swap the old list head, inserting this awaiter
      // as the new list head.
      // 使用release是为了确保set()能看到这里的写入，即加入链表
    } while (!m_event.m_state.compare_exchange_weak(
              oldValue,
              this,
              std::memory_order_release,
              std::memory_order_acquire));

    // Successfully enqueued. Remain suspended.
    return true;
  }

  // co_await仅需等待，没有返回值，因此await_resume()返回void即可
  void await_resume() noexcept {}

  // awaiting的具体对象
  const async_manual_reset_event& m_event;
  std::coroutine_handle<> m_awaitingCoroutine;
  // 链表的形式保存一系列awaiter
  awaiter* m_next;
};

async_manual_reset_event::awaiter async_manual_reset_event::operator co_await() const noexcept
{
  return awaiter{*this};
}

void async_manual_reset_event::set() noexcept
{
  // Needs to be 'release' so that subsequent 'co_await' has
  // visibility of our prior writes.
  // Needs to be 'acquire' so that we have visibility of prior
  // writes by awaiting coroutines.
  void* oldValue = m_state.exchange(this, std::memory_order_acq_rel);
  if (oldValue != this) {
    // Wasn't already in 'set' state.
    // Treat old value as head of a linked-list of waiters
    // which we have now acquired and need to resume.
    auto* waiters = static_cast<awaiter*>(oldValue);
    while (waiters != nullptr) {
      // Read m_next before resuming the coroutine as resuming
      // the coroutine will likely destroy the awaiter object.
      auto* next = waiters->m_next;
      waiters->m_awaitingCoroutine.resume();
      // 一旦awaiter被析构，如果这里再使用waiters->m_next就会segfault
      waiters = next;
    }
  }
}

// A simple task-class for void-returning coroutines.
struct task
{
  struct promise_type
  {
    task get_return_object() { return {}; }
    std::suspend_never initial_suspend() { return {}; }
    std::suspend_never final_suspend() { return {}; }
    void return_void() {}
    void unhandled_exception() {}
  };
};

int value = 0;
async_manual_reset_event event;

// A single call to produce a value
void producer()
{
  std::cout << "producing value..." << std::endl;
  std::this_thread::sleep_for(std::chrono::milliseconds{500});
  value = 1; // some_long_running_computation;

  // Publish the value by setting the event.
  event.set();
}

// Supports multiple concurrent consumers
task consumer()
{
  // Wait until the event is signalled by call to event.set()
  // in the producer() function.
  co_await event;

  // Now it's safe to consume 'value'
  // This is guaranteed to 'happen after' assignment to 'value'
  std::cout << "value found: " << value << std::endl;
}

int main(int argc, char **argv)
{
  consumer();
  consumer();
  consumer();
  consumer();

  producer();

  consumer();
  return 0;
}