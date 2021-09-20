#include "corormc.h"
#include "utils/utils.h"
#include <chrono>
#include <coroutine>
#include <iostream>

const unsigned int NUM_REPS = 1000000;

class MinimalCoroRMC {
public:
  /* must have this name */
  struct promise_type {
    /* move assignment op */
    promise_type &operator=(promise_type &&oth) = delete;
    /* move constructor */
    promise_type(promise_type &&oth) = delete;
    /* copy constructor */
    promise_type(const promise_type &) = delete;
    /* copy assignment op */
    promise_type &operator=(const promise_type &) = delete;

    /* constructor */
    promise_type() noexcept {};
    ~promise_type() = default;

    /* suspend coroutine on creation */
    auto initial_suspend() { return std::suspend_always{}; }
    /* suspend after coroutine ends */
    auto final_suspend() noexcept { return std::suspend_always{}; }
    /* must return the object that wraps promise_type */
    auto get_return_object() noexcept { return MinimalCoroRMC{*this}; }
    void return_void() {}
    void unhandled_exception() noexcept { std::terminate(); }
  };

  using HDL = std::coroutine_handle<promise_type>;

  /* default constructor */
  MinimalCoroRMC() = delete;
  /* move assignment op */
  MinimalCoroRMC &operator=(MinimalCoroRMC &&oth) = delete;
  /* copy constructor */
  MinimalCoroRMC(const MinimalCoroRMC &) = delete;
  /* copy assignment op */
  MinimalCoroRMC &operator=(const MinimalCoroRMC &) = delete;

  ~MinimalCoroRMC() {}

  void *operator new(size_t size) = delete;
  void operator delete(void *p) = delete;

  auto &get_handle() { return _coroutine; }

private:
  MinimalCoroRMC(promise_type &p) : _coroutine(HDL::from_promise(p)) {}
  HDL _coroutine;
};

inline MinimalCoroRMC minimal_coro() {
  for (auto i = 0u; i < NUM_REPS / 2; ++i)
    co_await std::suspend_always{};
}

int main() {
  auto rmc = minimal_coro();
  auto &h = rmc.get_handle();
  time_point start;
  long long duration;

  start = time_start();
  for (auto i = 0u; i < NUM_REPS / 2; ++i)
    h.resume();
  duration = time_end(start);
  h.destroy();

  std::cout << "duration=" << duration << " ns\n";
  std::cout << "per yield=" << duration / (double)NUM_REPS << " ns\n";
}
