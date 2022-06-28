#pragma once

#include <coroutine>

#include "allocator.h"
#include "config.h"
#include "utils/utils.h"

/* RMC allocator */
inline thread_local RMCAllocator allocator;
struct InitReply;

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
    std::coroutine_handle<promise_type> continuation;
    void *reply_ptr = nullptr;
    int param = 0;
    bool waiting_mem_access = false;
    bool init_reply = false;
    uint8_t reply_sz = 0;

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

    void *operator new(size_t size) { return allocator.alloc(size); }
    void operator delete(void *p, size_t size) { allocator.free(p, size); }

    /* suspend coroutine on creation */
    auto initial_suspend() { return std::suspend_always{}; }

    struct final_awaiter {
      bool await_ready() noexcept { return false; }

      std::coroutine_handle<> await_suspend(
          std::coroutine_handle<promise_type> h) noexcept {
        // if there is a continuation to resume, resume it but clear it first
        // so that the scheduler doesn't attempt to resume it anymore.
        if (h.promise().continuation) {
          // if the coro that is about to die has a continuation, then it
          // must be the case that the continuation's continuation is the
          // CoroRMC that is in the runqueue (given RMCAwaiter::await_suspend())
          // therefore, clear the continuation's continuation
          h.promise().continuation.promise().continuation = {};
          return h.promise().continuation;
        } else {
          return std::noop_coroutine();  // return to scheduler
        }
      }

      void await_resume() noexcept {}
    };

    /* when a CoroRMC is about to suspend for the final time, check if there's
       a CoroRMC to return to */
    final_awaiter final_suspend() noexcept { return {}; }
    /* must return the object that wraps promise_type */
    auto get_return_object() noexcept { return CoroRMC{*this}; }
    void unhandled_exception() noexcept { std::terminate(); }

    template <typename T>
    void return_value(T *reply) {
      /* reply_ptr will point to a CoroRMC-local variable. This is valid
       * because we don't destroy the coroutine on final_suspend(), instead it
       * is explicitly destroyed by the scheduler after issuing a reply (which
       * actually copies the memory to reply buffer) */
      static_assert(sizeof(T) <= MAX_RMC_REPLY_LEN);

      if (reply != nullptr) {
        reply_ptr = reply;
        reply_sz = sizeof(T);
        if constexpr (std::is_same<T, InitReply>::value) init_reply = true;
      }
    }
  };

  using coro_handle = std::coroutine_handle<promise_type>;

  /* Awaiter for other CoroRMCs */
  class RMCAwaiter {
   public:
    bool await_ready() noexcept { return false; }

    coro_handle await_suspend(coro_handle callee) noexcept {
      // Store the continuation in the task's promise so that the
      // final_suspend() knows to resume this coroutine when the task completes.
      called.promise().continuation = callee;
      // Also, store the new coro in the continuation's continuation
      // so we can resume it from scheduler
      callee.promise().continuation = called;
      // Then we resume the task's coroutine, which is currently suspended
      // at the initial-suspend-point (ie. at the open curly brace).
      return called;
    }

    void await_resume() noexcept {}

   private:
    friend CoroRMC;
    explicit RMCAwaiter(coro_handle h) noexcept : called(h) {}

    coro_handle called;
  };

  RMCAwaiter operator co_await() &&noexcept {
    // _coroutine here is the CoroRMC's coroutine that was just created,
    // the one we just called.
    return RMCAwaiter{_coroutine};
  }

  /* move constructor */
  CoroRMC(CoroRMC &&oth) noexcept : _coroutine(oth._coroutine) {
    oth._coroutine = nullptr;
  }

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
  CoroRMC(promise_type &p) : _coroutine(coro_handle::from_promise(p)) {}
  coro_handle _coroutine;
};
