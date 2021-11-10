#pragma once

#include "corormc.h"
#include "onesidedclient.h"

struct LLNode {
  void *next;
  uint64_t data;
};

static constexpr const uint16_t LINKDLIST_NUM_SKIP_NODES = 16;
static constexpr const uint32_t LINKDLIST_TOTAL_NODES =
    RDMA_BUFF_SIZE / sizeof(LLNode);

static_assert(sizeof(LLNode) == 16);

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
#if defined(WORKLOAD_HASHTABLE)
  struct cuckoo_hash table;
#endif

  Backend(OneSidedClient &c)
      : OSClient(c), base_laddr(0), base_raddr(0), last_random_addr(0) {
    LOG("Using interleaving RDMA Backend (default)");
  }
  ~Backend() {}

  void init() {
    base_laddr = OSClient.get_local_base_addr();
    base_raddr = OSClient.get_remote_base_addr();
    last_random_addr = base_raddr;

#if defined(WORKLOAD_HASHTABLE)
    // no need to destroy the table since we are giving it preallocated memory
    cuckoo_hash_init(&table, 16, reinterpret_cast<void *>(base_laddr));
#endif
  }

  auto get_param() noexcept { return AwaitGetParam{}; }

  auto read(uintptr_t raddr, uint32_t sz) noexcept {
    uintptr_t laddr = base_laddr + (raddr - base_raddr);
    OSClient.read_async(raddr, laddr, sz);
    return AwaitRDMARead{laddr};
  }

  // TODO: call read() above here
  auto read_laddr(uintptr_t laddr, uint32_t sz) noexcept {
    uintptr_t raddr = laddr - base_laddr + base_raddr;
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

  // TODO: call write() above here
  auto write_laddr(uintptr_t laddr, void *data, uint32_t sz) noexcept {
    uintptr_t raddr = laddr - base_laddr + base_raddr;
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

  template <bool suspend> struct AwaitDRAMWrite {
    void *laddr;
    void *data;
    uint32_t sz;

    AwaitDRAMWrite(void *laddr, void *data, uint32_t sz)
        : laddr(laddr), data(data), sz(sz) {}
    bool await_ready() { return false; }
    auto await_suspend(std::coroutine_handle<> coro) {
      return suspend; // suspend (true) or not (false)
    }
    void await_resume() {
      // copy the data
      memcpy(reinterpret_cast<void *>(laddr), data, sz);
    }
  };

public:
#if defined(WORKLOAD_HASHTABLE)
  struct cuckoo_hash table;
#endif

  Backend(OneSidedClient &c) : buffer(nullptr), linkedlist(nullptr) {
    buffer = huge.get();
    LOG("Using local DRAM Backend");
  }

  ~Backend() { destroy_linkedlist(linkedlist); }

  void init() {
#if defined(WORKLOAD_HASHTABLE)
    // no need to destroy the table since we are giving it preallocated memory
    cuckoo_hash_init(&table, 16, static_cast<void *>(buffer));
#else
    linkedlist = create_linkedlist<LLNode>(buffer, RDMA_BUFF_SIZE);
#endif
  }

  auto get_param() noexcept { return AwaitGetParam{}; }

  auto read(uintptr_t addr, uint32_t sz) noexcept {
    // interleaved: uncomment next
    for (auto cl = 0u; cl < sz; cl += 64)
      __builtin_prefetch(reinterpret_cast<void *>(addr + cl), 0, 0);
    return AwaitAddr<true>{addr};
    // run to completion: uncomment next
    // return AwaitAddr<false>{addr};
  }

  // with local memory raddr=laddr
  auto read_laddr(uintptr_t laddr, uint32_t sz) noexcept {
    return read(laddr, sz);
  }

  template <typename T> auto write(uintptr_t raddr, T *data) noexcept {
    DIE("not implemented yet");
    return AwaitVoid<false>{};
  }

  auto write_laddr(uintptr_t laddr, void *data, uint32_t sz) noexcept {
    if (sz > 64)
      DIE("sz > 64 write_laddr()");

    void *addr = reinterpret_cast<void *>(laddr);
    // interleaved: uncomment next
    __builtin_prefetch(addr, 1, 0);
    return AwaitDRAMWrite<true>{addr, data, sz};
    // run to completion: uncomment next
    // return AwaitDRAMWrite<false>{addr, data, sz};
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
