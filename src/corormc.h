#pragma once

#include "allocator.h"
#include "corohashtable.h"
#include "lib/cuckoo_hash.h"
#include "onesidedclient.h"
#include "utils/utils.h"
#include <atomic>
#include <coroutine>
#include <shared_mutex>

/* TODO: move these to a better place */
static constexpr const uint16_t PAGE_SIZE = 4096;
static constexpr const size_t RDMA_BUFF_SIZE = 1 << 30;
static constexpr const uint16_t LINKDLIST_NUM_SKIP_NODES = 16;
static constexpr const uint32_t LINKDLIST_TOTAL_NODES =
    RDMA_BUFF_SIZE / sizeof(LLNode);

/* for read-write lock support */
inline static std::atomic<uint64_t> readers;
inline static std::atomic<uint64_t> awaiting_writers;
inline static std::shared_mutex rw_mutex;

/* RMC allocator */
inline thread_local RMCAllocator allocator;

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
    bool waiting_mem_access = false;
    int reply_val = 0;
    int param = 0;

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

      std::coroutine_handle<>
      await_suspend(std::coroutine_handle<promise_type> h) noexcept {
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
          return std::noop_coroutine(); // return to scheduler
        }
      }

      void await_resume() noexcept {}
    };

    /* when a CoroRMC is about to suspend for the final time, check if there's
       a CoroRMC to return to */
    final_awaiter final_suspend() noexcept { return {}; }
    /* must return the object that wraps promise_type */
    auto get_return_object() noexcept { return CoroRMC{*this}; }
    void return_void() {}
    void unhandled_exception() noexcept { std::terminate(); }
    /* yields don't suspend */
    auto yield_value(int val) {
      reply_val = val;
      return std::suspend_never{};
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
  CoroRMC(promise_type &p) : _coroutine(coro_handle::from_promise(p)) {}
  coro_handle _coroutine;
};

struct AwaitGetParam {
  CoroRMC::promise_type *promise;

  bool await_ready() { return false; }
  auto await_suspend(std::coroutine_handle<CoroRMC::promise_type> coro) {
    promise = &coro.promise();
    return false; // don't suspend
  }
  int await_resume() { return promise->param; }
};

template <bool suspend> struct AwaitVoid {
  AwaitVoid() {}
  bool await_ready() { return false; }
  auto await_suspend(std::coroutine_handle<> coro) {
    return suspend; // suspend (true) or not (false)
  }
  void await_resume() {}
};

/* used when resume returns an address */
template <bool suspend> struct AwaitAddr {
  uintptr_t addr;

  AwaitAddr(uintptr_t addr) : addr(addr) {}
  bool await_ready() { return false; }
  auto await_suspend(std::coroutine_handle<> coro) {
    return suspend; // suspend (true) or not (false)
  }
  void *await_resume() { return reinterpret_cast<void *>(addr); }
};

static inline uint32_t get_next_llnode(uint32_t num_skip) noexcept {
  thread_local uint32_t addresses_given = 0;
  uint32_t next_node = addresses_given * LINKDLIST_NUM_SKIP_NODES;

  if (next_node + num_skip > LINKDLIST_TOTAL_NODES) {
    next_node = 0;
    addresses_given = 0;
  }

  addresses_given++;
  return next_node;
}

/* Generic class for Backends; needs full specialization */
template <class A> class Backend {};

/* Backend<OneSidedClient> is our main async rdma backend */
template <> class Backend<OneSidedClient> {
protected:
  OneSidedClient &OSClient;
  uintptr_t base_laddr;
  uintptr_t base_raddr;
  uintptr_t last_random_addr;

  struct AwaitRDMARead {
    uintptr_t addr;
    CoroRMC::promise_type *promise;

    AwaitRDMARead(uintptr_t addr) : addr(addr) {}
    bool await_ready() { return false; }
    auto await_suspend(std::coroutine_handle<CoroRMC::promise_type> coro) {
      promise = &coro.promise();

      // this is a read coming from a nested CoroRMC, need the caller's
      // promise
      if (promise->continuation)
        promise = &promise->continuation.promise();

      promise->waiting_mem_access = true;
      return true; // suspend (true)
    }
    void *await_resume() {
      promise->waiting_mem_access = false;
      return reinterpret_cast<void *>(addr);
    }
  };

  struct AwaitRDMAWrite {
    CoroRMC::promise_type *promise;

    AwaitRDMAWrite() {}
    bool await_ready() { return false; }
    auto await_suspend(std::coroutine_handle<CoroRMC::promise_type> coro) {
      promise = &coro.promise();

      // this is a write coming from a nested CoroRMC, need the caller's
      // promise
      if (promise->continuation)
        promise = &promise->continuation.promise();

      promise->waiting_mem_access = true;
      return true; // suspend (true)
    }
    void await_resume() { promise->waiting_mem_access = false; }
  };

public:
  struct cuckoo_hash table;

  Backend(OneSidedClient &c)
      : OSClient(c), base_laddr(0), base_raddr(0), last_random_addr(0) {
    LOG("Using interleaving RDMA Backend (default)");
  }
  ~Backend() {}

  void init() {
    base_laddr = OSClient.get_local_base_addr();
    base_raddr = OSClient.get_remote_base_addr();
    std::cout << "base_laddr=" << std::hex << base_laddr << "\n";
    std::cout << "base_raddr=" << std::hex << base_raddr << "\n";
    last_random_addr = base_raddr;

    // no need to destroy the table since we are giving it preallocated memory
    cuckoo_hash_init(&table, 16, reinterpret_cast<void *>(base_laddr));
  }

  auto get_param() noexcept { return AwaitGetParam{}; }

  auto read(uintptr_t raddr, uint32_t sz) noexcept {
    uintptr_t laddr = base_laddr + (raddr - base_raddr);
    OSClient.read_async(raddr, laddr, sz);
    return AwaitRDMARead{laddr};
  }

  auto read_laddr(uintptr_t laddr, uint32_t sz) noexcept {
    uintptr_t raddr = laddr - base_laddr + base_raddr;
    //std::cout << "read_laddr laddr=" << laddr << " raddr=" << raddr << "\n";
    OSClient.read_async(raddr, laddr, sz);
    return AwaitRDMARead{laddr};
  }

  template <typename T> auto write(uintptr_t raddr, T *data) noexcept {
    // static_assert(sizeof(T) <= 8);
    uintptr_t laddr = base_laddr + (raddr - base_raddr);

    /* copy to laddr first, then issue the write from there */
    //*(reinterpret_cast<T *>(laddr)) = *data;
    memcpy(reinterpret_cast<void *>(laddr), data, sizeof(T));
    OSClient.write_async(raddr, laddr, sizeof(T));
    return AwaitRDMAWrite{};
  }

  auto write_laddr(uintptr_t laddr, void *data, uint32_t sz) noexcept {
    uintptr_t raddr = laddr - base_laddr + base_raddr;
    //std::cout << "write_laddr laddr=" << laddr << " raddr=" << raddr << "\n";
    memcpy(reinterpret_cast<void *>(laddr), data, sz);
    OSClient.write_async(raddr, laddr, sz);
    return AwaitRDMAWrite{};
  }

  // TODO: unify with get_random_addr
  auto get_baseaddr(uint32_t num_nodes) noexcept {
    uint32_t next_node = get_next_llnode(num_nodes);
    return base_raddr + next_node * sizeof(LLNode);
  }

  uintptr_t get_random_addr() {
    last_random_addr += 248;
    return last_random_addr;
  }

  uintptr_t get_base_raddr() noexcept { return base_raddr; }
};

/* Backend<SyncRDMA> defines a backend that uses the async rdma backend
   synchronously */
class SyncRDMA {};
template <> class Backend<SyncRDMA> {
  OneSidedClient &OSClient;
  uintptr_t base_laddr;
  uintptr_t base_raddr;
  RDMAClient &rclient;
  RDMAContext *ctx;
  ibv_cq_ex *send_cq;

public:
  Backend(OneSidedClient &c)
      : OSClient(c), base_laddr(0), base_raddr(0),
        rclient(OSClient.get_rclient()) {
    LOG("Using run-to-completion RDMA Backend");
  }
  ~Backend() {}

  void init() {
    base_laddr = OSClient.get_local_base_addr();
    base_raddr = OSClient.get_remote_base_addr();
    ctx = &rclient.get_context(0);
    send_cq = rclient.get_send_cq(0);
  }

  auto get_param() noexcept { return AwaitGetParam{}; }

  auto read(uintptr_t raddr, uint32_t sz) noexcept {
    uintptr_t laddr = base_laddr + (raddr - base_raddr);
    rclient.start_batched_ops(ctx);
    OSClient.read_async(raddr, laddr, sz);
    rclient.end_batched_ops();

    rclient.poll_atleast(1, send_cq, [](size_t) constexpr->void{});
    return AwaitAddr<false>{laddr};
  }

  template <typename T> auto write(uintptr_t raddr, T *data) noexcept {
    DIE("not implemented yet");
    return AwaitVoid<true>{};
  }

  auto get_baseaddr(uint32_t num_nodes) noexcept {
    uint32_t next_node = get_next_llnode(num_nodes);
    return base_raddr + next_node * sizeof(LLNode);
  }

  uintptr_t get_random_addr() {
    DIE("not implemented yet");
    return 0;
  }
};

/* Backend<Threading> defines a backend that simulates context switching
   threads by sleeping before suspending and resuming */
class Threading {};
template <> class Backend<Threading> {
protected:
  /* one-way delay of switching to a thread */
  static constexpr const uint64_t ONEWAY_DELAY_NS = 200;

  OneSidedClient &OSClient;
  uintptr_t base_laddr;
  uintptr_t base_raddr;
  long long oneway_delay_cycles;

  struct AwaitAddrDelayed {
    uintptr_t addr;
    long long oneway_delay;

    AwaitAddrDelayed(uintptr_t addr, long long delay)
        : addr(addr), oneway_delay(delay) {}
    bool await_ready() { return false; }
    auto await_suspend(std::coroutine_handle<> coro) {
      spinloop_cycles(oneway_delay);
      return true;
    }
    void *await_resume() {
      spinloop_cycles(oneway_delay);
      return reinterpret_cast<void *>(addr);
    }
  };

public:
  Backend(OneSidedClient &c) : OSClient(c), base_laddr(0), base_raddr(0) {
    auto cpufreq = get_freq();
    oneway_delay_cycles = ns_to_cycles(ONEWAY_DELAY_NS, cpufreq);
    LOG("Using threads interleaving RDMA Backend");
    LOG("CPU freq=" << cpufreq);
    LOG("One-way delay in ns=" << ONEWAY_DELAY_NS
                               << ". Delay in cycles=" << oneway_delay_cycles);
  }

  ~Backend() {}

  void init() {
    base_laddr = OSClient.get_local_base_addr();
    base_raddr = OSClient.get_remote_base_addr();
  }

  auto get_param() noexcept { return AwaitGetParam{}; }

  auto read(uintptr_t raddr, uint32_t sz) noexcept {
    uintptr_t laddr = base_laddr + (raddr - base_raddr);
    OSClient.read_async(raddr, laddr, sz);
    return AwaitAddrDelayed{laddr, oneway_delay_cycles};
  }

  template <typename T> auto write(uintptr_t raddr, T *data) noexcept {
    DIE("not implemented yet");
    return AwaitVoid<true>{};
  }

  auto get_baseaddr(uint32_t num_nodes) noexcept {
    uint32_t next_node = get_next_llnode(num_nodes);
    return base_raddr + next_node * sizeof(LLNode);
  }

  uintptr_t get_random_addr() {
    DIE("not implemented yet");
    return 0;
  }
};

/* Backend<LocalMemory> defines a DRAM backend that runs coroutines to
 * completion */
class LocalMemory {};
template <> class Backend<LocalMemory> {
  char *buffer;
  LLNode *linkedlist;
  HugeAllocator huge;

public:
  Backend(OneSidedClient &c) : buffer(nullptr), linkedlist(nullptr) {
    buffer = huge.get();
    linkedlist = create_linkedlist<LLNode>(buffer, RDMA_BUFF_SIZE);
    LOG("Using local DRAM Backend");
  }

  ~Backend() { destroy_linkedlist(linkedlist); }

  void init() {}

  auto get_param() noexcept { return AwaitGetParam{}; }

  auto read(uintptr_t addr, uint32_t sz) noexcept {
    //__builtin_prefetch(reinterpret_cast<void *>(addr), 0, 0);
    // return AwaitAddr<true>{addr};
    return AwaitAddr<false>{addr};
  }

  template <typename T> auto write(uintptr_t raddr, T *data) noexcept {
    DIE("not implemented yet");
    return AwaitVoid<true>{};
  }

  auto get_baseaddr(uint32_t num_nodes) noexcept {
    uint32_t next_node = get_next_llnode(num_nodes);
    return reinterpret_cast<uintptr_t>(buffer + next_node * sizeof(LLNode));
  }

  uintptr_t get_random_addr() {
    DIE("not implemented yet");
    return 0;
  }
};

inline CoroRMC read_lock() {
  while (awaiting_writers.load(std::memory_order_consume) > 0 ||
         !rw_mutex.try_lock_shared())
    co_await std::suspend_always{};

  readers++;
}

inline CoroRMC write_lock() {
  awaiting_writers++;
  while (!rw_mutex.try_lock())
    co_await std::suspend_always{};
}

static inline void read_unlock() {
  rw_mutex.unlock_shared();
  readers--;
}

static inline void write_unlock() {
  rw_mutex.unlock();
  awaiting_writers--;
}

template <class T> inline CoroRMC traverse_linkedlist(Backend<T> &b) {
  int num_nodes = co_await b.get_param();
  uintptr_t addr = b.get_baseaddr(num_nodes);
  LLNode *node = nullptr;

  for (int i = 0; i < num_nodes; ++i) {
    node = static_cast<LLNode *>(co_await b.read(addr, sizeof(LLNode)));
    addr = reinterpret_cast<uintptr_t>(node->next);
  }

  co_yield 1;
}

template <class T> inline CoroRMC random_writes(Backend<T> &b) {
  const uint32_t num_writes = co_await b.get_param();
  uint64_t val = 0xDEADBEEF;

  for (auto i = 0u; i < num_writes; ++i) {
    co_await b.write(b.get_random_addr(), &val);
  }

  co_yield 1;
}

template <class T> inline CoroRMC lock_traverse_linkedlist(Backend<T> &b) {
  thread_local uint32_t num_execs = 0;
  int num_nodes = co_await b.get_param();
  uintptr_t addr = b.get_baseaddr(num_nodes);
  LLNode *node = nullptr;
  bool lockreads = true;

  if (++num_execs >= 10) {
    lockreads = false;
    num_execs = 0;
  }

  for (int i = 0; i < num_nodes; ++i) {
    if (lockreads)
      co_await read_lock();
    else
      co_await write_lock();

    node = static_cast<LLNode *>(co_await b.read(addr, sizeof(LLNode)));
    addr = reinterpret_cast<uintptr_t>(node->next);

    if (lockreads)
      read_unlock();
    else
      write_unlock();
  }

  co_yield 1;
}
