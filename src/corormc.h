#pragma once

#include "allocator.h"
#include "onesidedclient.h"
#include "utils/utils.h"
#include <coroutine>

inline RMCAllocator allocator;
inline bool runcoros;
/* pre allocated, free coroutines */
inline std::deque<std::coroutine_handle<>> freequeue;

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

/* TODO: move these to a better place */
static constexpr const uint16_t PAGE_SIZE = 4096;
static constexpr const size_t RDMA_BUFF_SIZE = 1 << 26;

/* common for backends */
struct AwaitNextReq {
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

template <class A> class Backend {};

template <> class Backend<OneSidedClient> {
  OneSidedClient &OSClient;

public:
  Backend(OneSidedClient &c) : OSClient(c) {}
  ~Backend() {}

  auto wait_next_req() noexcept { return AwaitNextReq{}; }
  auto read(uintptr_t addr, uint32_t sz) noexcept {
    struct AwaitHostMemoryRead {
      OneSidedClient &OSClient;
      uintptr_t raddr;
      uint32_t size;
      uintptr_t laddr;

      AwaitHostMemoryRead(OneSidedClient &c, uintptr_t raddr, uint32_t sz)
          : OSClient(c), raddr(raddr), size(sz) {
        laddr = OSClient.get_local_base_addr() +
                (raddr - OSClient.get_remote_base_addr());
      }

      bool await_ready() { return false; }

      auto await_suspend(std::coroutine_handle<> coro) {
        OSClient.read_async(raddr, laddr, size);
        return true; // suspend
      }

      void *await_resume() { return reinterpret_cast<void *>(laddr); }
    };

    return AwaitHostMemoryRead{OSClient, addr, sz};
  }
  auto get_baseaddr() noexcept { return OSClient.get_remote_base_addr(); }
};

class LocalMemory {};

template <> class Backend<LocalMemory> {
  char *buffer;
  LLNode *linkedlist;

public:
  Backend(OneSidedClient &c) : buffer(nullptr), linkedlist(nullptr) {
    buffer = static_cast<char *>(aligned_alloc(PAGE_SIZE, RDMA_BUFF_SIZE));
    linkedlist = create_linkedlist<LLNode>(buffer, RDMA_BUFF_SIZE);
  }

  ~Backend() {
    destroy_linkedlist(linkedlist);
    free(buffer);
  }

  auto wait_next_req() noexcept { return AwaitNextReq{}; }
  auto read(uintptr_t addr, uint32_t sz) noexcept {
    struct AwaitDRAMRead {
      uintptr_t laddr;

      AwaitDRAMRead(uintptr_t &a) : laddr(a) {}

      bool await_ready() { return false; }
      auto await_suspend(std::coroutine_handle<> coro) {
        return false; // don't suspend
      }
      void *await_resume() { return reinterpret_cast<void *>(laddr); }
    };

    return AwaitDRAMRead{addr};
  }
  auto get_baseaddr() noexcept { return reinterpret_cast<uintptr_t>(buffer); }
};

template <class T>
inline CoroRMC traverse_linkedlist(Backend<T> &b, size_t num_nodes) {
  while (co_await b.wait_next_req()) {
    // getting a local buffer should be explicit here
    // consider cpu locality into the design
    uintptr_t addr = b.get_baseaddr();
    LLNode *node = nullptr;

    for (size_t i = 0; i < num_nodes; ++i) {
      node = static_cast<LLNode *>(co_await b.read(addr, sizeof(LLNode)));
      addr = reinterpret_cast<uintptr_t>(node->next);
    }

    co_yield 1;
  }
}
