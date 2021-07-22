#ifndef CORORMC_H
#define CORORMC_H

#include "allocator.h"
#include "utils/utils.h"
#include <coroutine>

class CoroRMC {
public:
  /*
  based on:
      https://www.modernescpp.com/index.php/c-20-an-infinite-data-stream-with-coroutines
      https://github.com/andreasbuhr/cppcoro/blob/master/include/cppcoro/task.hpp
      https://github.com/GorNishanov/await/blob/master/2018_CppCon/src/coro_infra.h
  */

  /* must have this name */
  struct promise_type {
    bool waiting_next_req = false;

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

    void *operator new(size_t size) {
      if (size != 120)
        DIE("promise size is not 120, it is=" << size);

      return RMCAllocator::get_promise();
    }

    void operator delete(void *p) { RMCAllocator::delete_promise(p); }

    /* suspend coroutine on creation */
    auto initial_suspend() { return std::suspend_always{}; }

    /* don't suspend after coroutine ends */
    auto final_suspend() { return std::suspend_never{}; }

    /* must return the object that wraps promise_type */
    auto get_return_object() noexcept { return CoroRMC{*this}; }

    void return_void() {}

    void unhandled_exception() noexcept { std::terminate(); }
  };

  using HDL = std::coroutine_handle<promise_type>;

  /* move constructor */
  CoroRMC(CoroRMC &&oth) : _coroutine(oth._coroutine) { oth._coroutine = nullptr; }

  /* default constructor */
  CoroRMC() = delete;
  /* move assignment op */
  CoroRMC &operator=(CoroRMC &&oth) = delete;
  /* copy constructor */
  CoroRMC(const CoroRMC &) = delete;
  /* copy assignment op */
  CoroRMC &operator=(const CoroRMC &) = delete;

  ~CoroRMC() {}

  void *operator new(size_t size) = delete;
  void operator delete(void *p) = delete;

  auto get_handle() { return _coroutine; }

private:
  CoroRMC(promise_type &p) : _coroutine(HDL::from_promise(p)) {}

  HDL _coroutine;
};

#endif
