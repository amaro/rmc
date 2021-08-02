#pragma once

#include "allocator.h"
#include "onesidedclient.h"
#include "utils/utils.h"
#include <coroutine>

inline RMCAllocator allocator;
inline bool runcoros;
/* pre allocated, free coroutines */
inline std::deque<std::coroutine_handle<>> freequeue;
inline OneSidedClient *os_client;

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
    int reply_val = 0;

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
    /* don't suspend after coroutine ends */
    auto final_suspend() { return std::suspend_never{}; }
    /* must return the object that wraps promise_type */
    auto get_return_object() noexcept { return CoroRMC{*this}; }
    void return_void() {}
    void unhandled_exception() noexcept { std::terminate(); }
    auto yield_value(int val) {
      reply_val = val;
      return std::suspend_never{};
    }
  };

  using HDL = std::coroutine_handle<promise_type>;

  /* move constructor */
  CoroRMC(CoroRMC &&oth) : _coroutine(oth._coroutine) {
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
  CoroRMC(promise_type &p) : _coroutine(HDL::from_promise(p)) {}

  HDL _coroutine;
};

struct AwaitableNextReq {
  CoroRMC::promise_type *_promise;

  bool await_ready() { return false; }
  auto await_suspend(std::coroutine_handle<CoroRMC::promise_type> coro) {
    _promise = &coro.promise();
    _promise->waiting_next_req = true;
    freequeue.push_front(coro);
    return true; // suspend
  }
  bool await_resume() {
    _promise->waiting_next_req = false;
    return runcoros;
  }
};

inline auto wait_next_req() noexcept { return AwaitableNextReq{}; }

struct AwaitableHostMemoryRead {
  uintptr_t raddr;
  uintptr_t laddr;
  uint32_t size;

  AwaitableHostMemoryRead(uintptr_t raddr, uint32_t sz)
      : raddr(raddr), size(sz) {
    /* find the remote offset from remote base addr and apply it to local addr
     * too */
    uint32_t offset = raddr - os_client->get_remote_base_addr();
    laddr = os_client->get_local_base_addr() + offset;
  }

  bool await_ready() { return false; }
  auto await_suspend(std::coroutine_handle<> coro) {
    os_client->read_async(raddr, laddr, size);
    return true; // suspend
  }
  void *await_resume() { return reinterpret_cast<void *>(laddr); }
};

inline auto read(uintptr_t raddr, uint32_t sz) noexcept {
  return AwaitableHostMemoryRead{raddr, sz};
}

inline auto get_remote_baseaddr() noexcept {
  return os_client->get_remote_base_addr();
}

inline CoroRMC traverse_linkedlist(size_t num_nodes) {
  while (co_await wait_next_req()) {
    // getting a local buffer should be explicit here
    // consider cpu locality into the design
    uintptr_t remote_addr = get_remote_baseaddr();
    LLNode *node = nullptr;

    for (size_t i = 0; i < num_nodes; ++i) {
      node = static_cast<LLNode *>(co_await read(remote_addr, sizeof(LLNode)));
      remote_addr = reinterpret_cast<uintptr_t>(node->next);
    }

    co_yield 1;
  }
}
