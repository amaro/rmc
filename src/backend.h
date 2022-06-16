#pragma once

#include "corormc.h"
#include "onesidedclient.h"
#if defined(WORKLOAD_HASHTABLE)
#include "lib/cuckoo_hash.h"
#endif

struct LLNode {
  void *next;
  uint64_t data;
};

static constexpr const uint16_t LINKDLIST_NUM_SKIP_NODES = 16;
static constexpr const uint32_t LINKDLIST_TOTAL_NODES =
    RMCK_APPS_BUFF_SZ / sizeof(LLNode);

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

struct AwaitGetParam {
  CoroRMC::promise_type *promise;

  bool await_ready() { return false; }
  auto await_suspend(std::coroutine_handle<CoroRMC::promise_type> coro) {
    promise = &coro.promise();
    return false;  // don't suspend
  }
  int await_resume() { return promise->param; }
};

template <bool suspend>
struct AwaitVoid {
  AwaitVoid() {}
  bool await_ready() { return false; }
  auto await_suspend(std::coroutine_handle<> coro) {
    return suspend;  // suspend (true) or not (false)
  }
  void await_resume() {}
};

/* used when resume returns an address */
template <bool suspend>
struct AwaitAddr {
  uintptr_t addr;

  AwaitAddr(uintptr_t addr) : addr(addr) {}
  bool await_ready() { return false; }
  auto await_suspend(std::coroutine_handle<> coro) {
    return suspend;  // suspend (true) or not (false)
  }
  void *await_resume() { return reinterpret_cast<void *>(addr); }
};

/* Generic class for Backends; needs full specialization */
template <class A>
class Backend {};

/* Backend<OneSidedClient> is our main async rdma backend */
template <>
class Backend<OneSidedClient> {
 private:
  OneSidedClient &OSClient;
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
      if (promise->continuation) promise = &promise->continuation.promise();

      promise->waiting_mem_access = true;
      return true;  // suspend (true)
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
      if (promise->continuation) promise = &promise->continuation.promise();

      promise->waiting_mem_access = true;
      return true;  // suspend (true)
    }
    void await_resume() { promise->waiting_mem_access = false; }
  };

 public:
  uintptr_t apps_base_laddr;
  uintptr_t apps_base_raddr;
  uintptr_t rsvd_base_laddr;
  uintptr_t rsvd_base_raddr;

#if defined(WORKLOAD_HASHTABLE)
  struct cuckoo_hash table;
#endif

  Backend(OneSidedClient &c)
      : OSClient(c),
        last_random_addr(0),
        apps_base_laddr(0),
        apps_base_raddr(0),
        rsvd_base_laddr(0),
        rsvd_base_raddr(0) {
    printf("Using interleaving RDMA Backend (default)\n");
  }
  ~Backend() {}

  void init() {
    apps_base_laddr = OSClient.get_apps_base_laddr();
    apps_base_raddr = OSClient.get_apps_base_raddr();
    rsvd_base_laddr = OSClient.get_rsvd_base_laddr();
    rsvd_base_raddr = OSClient.get_rsvd_base_raddr();
    last_random_addr = apps_base_raddr;

#if defined(WORKLOAD_HASHTABLE)
    // no need to destroy the table since we are giving it preallocated memory
    cuckoo_hash_init(&table, 16, reinterpret_cast<void *>(apps_base_laddr));
#endif
  }

  auto get_param() noexcept { return AwaitGetParam{}; }

  auto read(uintptr_t raddr, uint32_t sz) noexcept {
    uintptr_t laddr = apps_base_laddr + (raddr - apps_base_raddr);
    OSClient.read_async(raddr, laddr, sz);
    return AwaitRDMARead{laddr};
  }

  auto read_laddr(uintptr_t laddr, uint32_t sz) noexcept {
    uintptr_t raddr = laddr - apps_base_laddr + apps_base_raddr;
    OSClient.read_async(raddr, laddr, sz);
    return AwaitRDMARead{laddr};
  }

  auto write(uintptr_t raddr, uintptr_t laddr, const void *data,
             uint32_t sz) noexcept {
    memcpy(reinterpret_cast<void *>(laddr), data, sz);
    OSClient.write_async(raddr, laddr, sz);
    return AwaitRDMAWrite{};
  }

  auto write_raddr(uintptr_t raddr, const void *data, uint32_t sz) noexcept {
    return write(raddr, apps_base_laddr + (raddr - apps_base_raddr), data, sz);
  }

  auto write_laddr(uintptr_t laddr, const void *data, uint32_t sz) noexcept {
    return write(laddr - apps_base_laddr + apps_base_raddr, laddr, data, sz);
  }

  // NOTE: cmp_swp does not map 1:1 raddrs to laddrs
  // raddr is fixed but laddr changes depending on in flight atomic ops
  auto cmp_swp(uintptr_t raddr, uintptr_t laddr, uint64_t cmp, uint64_t swp) {
    OSClient.cmp_swp_async(raddr, laddr, cmp, swp);
    return AwaitRDMARead{laddr};
  }

  // TODO: unify with get_random_raddr
  auto get_baseaddr(uint32_t num_nodes) noexcept {
    uint32_t next_node = get_next_llnode(num_nodes);
    return apps_base_raddr + next_node * sizeof(LLNode);
  }

  uintptr_t get_random_raddr() {
    last_random_addr += 248;
    return last_random_addr;
  }
};

/* Backend<SyncRDMA> defines a backend that uses the async rdma backend
   synchronously */
class SyncRDMA {};
template <>
class Backend<SyncRDMA> {
  OneSidedClient &OSClient;
  uintptr_t apps_base_laddr;
  uintptr_t apps_base_raddr;
  RDMAClient &rclient;
  RDMAContext *ctx;
  ibv_cq_ex *send_cq;

 public:
  Backend(OneSidedClient &c)
      : OSClient(c),
        apps_base_laddr(0),
        apps_base_raddr(0),
        rclient(OSClient.get_rclient()) {
    printf("Using run-to-completion RDMA Backend\n");
  }
  ~Backend() {}

  void init() {
    apps_base_laddr = OSClient.get_apps_base_laddr();
    apps_base_raddr = OSClient.get_apps_base_raddr();
    ctx = &rclient.get_context(0);
    send_cq = rclient.get_send_cq(0);
  }

  auto get_param() noexcept { return AwaitGetParam{}; }

  auto read(uintptr_t raddr, uint32_t sz) noexcept {
    uintptr_t laddr = apps_base_laddr + (raddr - apps_base_raddr);
    rclient.start_batched_ops(ctx);
    OSClient.read_async(raddr, laddr, sz);
    rclient.end_batched_ops();

    rclient.poll_atleast(1, send_cq, [](size_t) constexpr->void{});
    return AwaitAddr<false>{laddr};
  }

  auto write_raddr(uintptr_t raddr, void *data, uint32_t sz) noexcept {
    die("not implemented\n");
    return AwaitVoid<true>{};
  }

  auto write_laddr(uintptr_t laddr, void *data, uint32_t sz) noexcept {
    die("not implemented");
    return AwaitVoid<true>{};
  }

  auto get_baseaddr(uint32_t num_nodes) noexcept {
    uint32_t next_node = get_next_llnode(num_nodes);
    return apps_base_raddr + next_node * sizeof(LLNode);
  }

  uintptr_t get_random_raddr() {
    die("not implemented yet");
    return 0;
  }
};

/* Backend<Threading> defines a backend that simulates context switching
   threads by sleeping before suspending and resuming */
class Threading {};
template <>
class Backend<Threading> {
 private:
  /* one-way delay of switching to a thread */
  static constexpr const uint64_t ONEWAY_DELAY_NS = 200;

  OneSidedClient &OSClient;
  uintptr_t apps_base_laddr;
  uintptr_t apps_base_raddr;
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
  Backend(OneSidedClient &c)
      : OSClient(c), apps_base_laddr(0), apps_base_raddr(0) {
    auto cpufreq = get_freq();
    oneway_delay_cycles = ns_to_cycles(ONEWAY_DELAY_NS, cpufreq);
    printf("Using threads interleaving RDMA Backend\n");
    printf("CPU freq=%lld\n", cpufreq);
    printf("One-way delay in ns=%lu; delay in cycles=%lld\n", ONEWAY_DELAY_NS,
           oneway_delay_cycles);
  }

  ~Backend() {}

  void init() {
    apps_base_laddr = OSClient.get_apps_base_laddr();
    apps_base_raddr = OSClient.get_apps_base_raddr();
  }

  auto get_param() noexcept { return AwaitGetParam{}; }

  auto read(uintptr_t raddr, uint32_t sz) noexcept {
    uintptr_t laddr = apps_base_laddr + (raddr - apps_base_raddr);
    OSClient.read_async(raddr, laddr, sz);
    return AwaitAddrDelayed{laddr, oneway_delay_cycles};
  }

  template <typename T>
  auto write(uintptr_t raddr, T *data) noexcept {
    die("not implemented yet");
    return AwaitVoid<true>{};
  }

  auto get_baseaddr(uint32_t num_nodes) noexcept {
    uint32_t next_node = get_next_llnode(num_nodes);
    return apps_base_raddr + next_node * sizeof(LLNode);
  }

  uintptr_t get_random_raddr() {
    die("not implemented yet");
    return 0;
  }
};

/* Backend<LocalMemory> defines a DRAM backend that runs coroutines to
 * completion */
class LocalMemory {};
template <>
class Backend<LocalMemory> {
  char *buffer;
  LLNode *linkedlist;
  HugeAllocator huge;

  template <bool suspend>
  struct AwaitDRAMWrite {
    void *laddr;
    void *data;
    uint32_t sz;

    AwaitDRAMWrite(void *laddr, void *data, uint32_t sz)
        : laddr(laddr), data(data), sz(sz) {}
    bool await_ready() { return false; }
    auto await_suspend(std::coroutine_handle<> coro) {
      return suspend;  // suspend (true) or not (false)
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
    printf("Using local DRAM Backend\n");
  }

  ~Backend() { destroy_linkedlist(linkedlist); }

  void init() {
#if defined(WORKLOAD_HASHTABLE)
    // no need to destroy the table since we are giving it preallocated memory
    cuckoo_hash_init(&table, 16, static_cast<void *>(buffer));
#else
    linkedlist = create_linkedlist<LLNode>(buffer, RMCK_APPS_BUFF_SZ);
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

  auto write_raddr(uintptr_t laddr, void *data, uint32_t sz) noexcept {
    die("not implemented yet");
    return AwaitVoid<false>{};
  }

  auto write_laddr(uintptr_t laddr, void *data, uint32_t sz) noexcept {
    assert(sz <= 64);

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

  uintptr_t get_random_raddr() {
    die("not implemented yet");
    return 0;
  }
};

#if defined(LOCATION_CLIENT)
/* When runtime is located at the client */
class RMCLock {
 public:
  RMCLock() {}

  ~RMCLock() {}

  inline CoroRMC lock(Backend<OneSidedClient> &b) {
    const uintptr_t lock_raddr = b.rsvd_base_raddr;
    const uintptr_t lock_laddr = get_lock_laddr(b);

    *(reinterpret_cast<uint64_t *>(lock_laddr)) = 1;
    co_await b.cmp_swp(lock_raddr, lock_laddr, 0, 1);
    while (*(reinterpret_cast<uint64_t *>(lock_laddr)) != 0) {
      co_await b.cmp_swp(lock_raddr, lock_laddr, 0, 1);
    }
  }

  inline CoroRMC unlock(Backend<OneSidedClient> &b) {
    const uint64_t unlocked = 0;
    const uintptr_t lock_raddr = b.rsvd_base_raddr;
    const uintptr_t unlock_laddr = get_unlock_laddr(b);

    co_await b.write(lock_raddr, unlock_laddr, &unlocked, sizeof(uint64_t));
  }

 private:
  inline uintptr_t get_lock_laddr(Backend<OneSidedClient> &b) {
    static std::atomic<uintptr_t> lock_laddr = 0;

    // use [b.rsvd_base_laddr, b.rsvd_base_laddr + RMCK_RESERVED_BUFF_SZ - 8) to
    // grant laddr locks
    if (lock_laddr == 0)
      lock_laddr = b.rsvd_base_laddr;
    else
      lock_laddr += sizeof(uint64_t);

    if (lock_laddr >
        b.rsvd_base_laddr + RMCK_RESERVED_BUFF_SZ - sizeof(uint64_t))
      lock_laddr = b.rsvd_base_laddr;

    return lock_laddr;
  }

  inline uintptr_t get_unlock_laddr(Backend<OneSidedClient> &b) {
    // unlock laddr
    return b.rsvd_base_laddr + RMCK_RESERVED_BUFF_SZ - sizeof(uint64_t);
  }
};
#else
/* When runtime is located at SmartNIC or server, we can use atomics to local
 * memory */
class RMCLock {
 public:
  RMCLock() {
    if (pthread_spin_init(&l, PTHREAD_PROCESS_PRIVATE) != 0)
      die("could not init spin lock");
  }

  ~RMCLock() { pthread_spin_destroy(&l); }

  template <class T>
  inline CoroRMC lock(Backend<T> &b) {
    while (pthread_spin_trylock(&l) != 0) co_await std::suspend_always{};
  }

  template <class T>
  inline CoroRMC unlock(Backend<T> &b) {
    pthread_spin_unlock(&l);
    co_await std::suspend_never{};
  }

 private:
  pthread_spinlock_t l;
};
#endif
