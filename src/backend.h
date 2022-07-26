#pragma once

#include <cstring>

#include "corormc.h"
#include "onesidedclient.h"
#include "rdma/rdmapeer.h"
#include "rmcs.h"
#if defined(WORKLOAD_HASHTABLE)
#include "lib/cuckoo_hash.h"
#endif

struct AwaitGetParam {
  CoroRMC::promise_type *promise;

  constexpr bool await_ready() { return false; }
  auto await_suspend(std::coroutine_handle<CoroRMC::promise_type> coro) {
    promise = &coro.promise();
    return false;  // don't suspend
  }
  int await_resume() { return promise->param; }
};

// TODO: AwaitRead follows the reqs of RDMA backend as of now. Need to
// specialize for other backends
struct AwaitRead {
  uintptr_t addr;
  bool should_suspend;
  // to handle continuations (e.g., locks)
  CoroRMC::promise_type *promise;

  AwaitRead(uintptr_t addr, bool suspend)
      : addr(addr), should_suspend(suspend) {}

  constexpr bool await_ready() { return false; }

  auto await_suspend(std::coroutine_handle<CoroRMC::promise_type> coro) {
    promise = &coro.promise();

    // this is a read coming from a nested CoroRMC, need the caller's
    // promise
    if (promise->continuation) promise = &promise->continuation.promise();

    promise->waiting_mem_access = true;
    return should_suspend;  // true = suspend; false = don't
  }

  void *await_resume() {
    promise->waiting_mem_access = false;
    return reinterpret_cast<void *>(addr);
  }
};

// TODO: AwaitWrite follows the reqs of RDMA backend as of now. Need to
// specialize for other backends
struct AwaitWrite {
  bool should_suspend;
  CoroRMC::promise_type *promise;

  AwaitWrite(bool suspend) : should_suspend(suspend) {}
  constexpr bool await_ready() { return false; }
  auto await_suspend(std::coroutine_handle<CoroRMC::promise_type> coro) {
    promise = &coro.promise();

    // this is a write coming from a nested CoroRMC, need the caller's
    // promise
    if (promise->continuation) promise = &promise->continuation.promise();

    promise->waiting_mem_access = true;
    return should_suspend;  // true = suspend; false = don't
  }
  constexpr void await_resume() { promise->waiting_mem_access = false; }
};

class BackendBase {
 protected:
  // TODO: once everything works, move below to CoopRDMA
  OneSidedClient &OSClient;

  BackendBase(OneSidedClient &c) : OSClient(c) {}

  virtual ~BackendBase() {}
  BackendBase(const BackendBase &) = delete;
  BackendBase(BackendBase &&) = delete;
  BackendBase &operator=(const BackendBase &) = delete;
  BackendBase &operator=(BackendBase &&) = delete;

 public:
  virtual AwaitRead read(uintptr_t raddr, void *lbuf, uint32_t sz,
                         uint32_t rkey) const = 0;
  virtual AwaitWrite write(uintptr_t raddr, const void *data, uint32_t sz,
                           uint32_t rkey) const = 0;
  auto get_param() const { return AwaitGetParam{}; }
};

/* Cooperative multi tasking RDMA backend */
class CoopRDMA : public BackendBase {
  using OneSidedOp = RDMAContext::OneSidedOp;

 public:
  /* Need to re-work this for Locks */
  uintptr_t rsvd_base_laddr = 0;
  uintptr_t rsvd_base_raddr = 0;

#if defined(WORKLOAD_HASHTABLE)
  struct cuckoo_hash table;
#endif

  CoopRDMA(OneSidedClient &c) : BackendBase(c) {
    puts("Backend: Cooperative RDMA (default)");
  }

  void init() {
    puts("Backend: Initializing");

    auto *buffer = get_frame_alloc().get_huge_buffer().get();
    rsvd_base_laddr = reinterpret_cast<uintptr_t>(buffer);
    rsvd_base_raddr = OSClient.get_rsvd_base_raddr();

#if defined(WORKLOAD_HASHTABLE)
    // no need to destroy the table since we are giving it preallocated
    memory cuckoo_hash_init(&table, 16,
                            reinterpret_cast<void *>(apps_base_laddr));
#endif
  }

  AwaitRead read(uintptr_t raddr, void *lbuf, uint32_t sz,
                 uint32_t rkey) const override {
    const uintptr_t laddr = reinterpret_cast<uintptr_t>(lbuf);

    OSClient.post_op_from_frame(OneSidedOp{.raddr = raddr,
                                           .laddr = laddr,
                                           .len = sz,
                                           .rkey = rkey,
                                           .cmp = 0,
                                           .swp = 0,
                                           .optype = OneSidedOp::OpType::READ});

    return AwaitRead{laddr, true};
  }

  AwaitWrite write(uintptr_t raddr, const void *lbuf, uint32_t sz,
                   uint32_t rkey) const override {
    const uintptr_t laddr = reinterpret_cast<uintptr_t>(lbuf);

    OSClient.post_op_from_frame(
        OneSidedOp{.raddr = raddr,
                   .laddr = laddr,
                   .len = sz,
                   .rkey = rkey,
                   .cmp = 0,
                   .swp = 0,
                   .optype = OneSidedOp::OpType::WRITE});
    return AwaitWrite{true};
  }

  // NOTE: cmp_swp does not map 1:1 raddrs to laddrs
  // raddr is fixed but laddr changes depending on in flight atomic ops
  auto cmp_swp(uintptr_t raddr, uintptr_t laddr, uint64_t cmp, uint64_t swp,
               uint32_t rkey) {
    OSClient.post_op_from_frame(
        OneSidedOp{.raddr = raddr,
                   .laddr = laddr,
                   .len = 8,
                   .rkey = rkey,
                   .cmp = cmp,
                   .swp = swp,
                   .optype = OneSidedOp::OpType::CMP_SWP});

    return AwaitRead{laddr, true};
  }
};

/* Run to completion RDMA backend */
class CompRDMA : public BackendBase {
  using OneSidedOp = RDMAContext::OneSidedOp;
  RDMAContext *ctx = nullptr;
  ibv_cq_ex *send_cq = nullptr;
  RDMAClient &rclient;

 public:
  CompRDMA(OneSidedClient &c)
      : BackendBase(c), rclient(OSClient.get_rclient()) {
    puts("Using run-to-completion RDMA Backend");
  }

  void init() {
    puts("Backend: Initializing");

    ctx = &rclient.get_context(0);
    send_cq = rclient.get_send_cq(0);
  }

  AwaitRead read(uintptr_t raddr, void *lbuf, uint32_t sz,
                 uint32_t rkey) const override {
    const uintptr_t laddr = reinterpret_cast<uintptr_t>(lbuf);
    rclient.start_batched_ops(ctx);
    OSClient.post_op_from_frame(OneSidedOp{.raddr = raddr,
                                           .laddr = laddr,
                                           .len = sz,
                                           .rkey = rkey,
                                           .cmp = 0,
                                           .swp = 0,
                                           .optype = OneSidedOp::OpType::READ});
    rclient.end_batched_ops();

    rclient.poll_atleast(1, send_cq, [](size_t) constexpr->void{});
    return AwaitRead{laddr, false};
  }

  AwaitWrite write(uintptr_t raddr, const void *lbuf, uint32_t sz,
                   uint32_t rkey) const override {
    const uintptr_t laddr = reinterpret_cast<uintptr_t>(lbuf);
    rclient.start_batched_ops(ctx);
    OSClient.post_op_from_frame(
        OneSidedOp{.raddr = raddr,
                   .laddr = laddr,
                   .len = sz,
                   .rkey = rkey,
                   .cmp = 0,
                   .swp = 0,
                   .optype = OneSidedOp::OpType::WRITE});
    rclient.end_batched_ops();
    rclient.poll_atleast(1, send_cq, [](size_t) constexpr->void{});

    return AwaitWrite{false};
  }
};

/* Backend for server location. A list of RMCs issue prefetch instructions for
 * their accesses and immediately suspend, giving time for cpu prefetchers to
 * get data closer. After all RMCs in the list prefetch, we resume them in
 * order, and actually access their data */
class PrefetchDRAM : public BackendBase {
  DramMrAllocator *_dram_allocator;
  std::vector<MemoryRegion> *_mrs;

 public:
  PrefetchDRAM(OneSidedClient &c) : BackendBase(c) {
    puts("Using prefetching DRAM Backend");
  }

  void init(DramMrAllocator *allocator, std::vector<MemoryRegion> *mrs) {
    _dram_allocator = allocator;
    _mrs = mrs;
  }

  AwaitRead read(uintptr_t raddr, [[maybe_unused]] void *lbuf, uint32_t sz,
                 uint32_t rkey) const override {
    for (auto cl = 0u; cl < sz; cl += 64)
      __builtin_prefetch(reinterpret_cast<void *>(raddr + cl), 0, 0);

    return AwaitRead{raddr, true};
  }

  AwaitWrite write(uintptr_t raddr, const void *lbuf, uint32_t sz,
                   uint32_t rkey) const override {
    assert(sz <= 64);

    puts("not fully implemented");
    //__builtin_prefetch(addr, 1, 0);
    std::memcpy(reinterpret_cast<void *>(raddr), lbuf, sz);
    return AwaitWrite{true};
  }
};

class CompDRAM : public BackendBase {
  DramMrAllocator *_dram_allocator;
  std::vector<MemoryRegion> *_mrs;

 public:
  CompDRAM(OneSidedClient &c) : BackendBase(c) {
    puts("Using run to completion DRAM Backend");
  }

  void init(DramMrAllocator *allocator, std::vector<MemoryRegion> *mrs) {
    puts("init run to completion DRAM Backend");
    _dram_allocator = allocator;
    _mrs = mrs;
  }

  AwaitRead read(uintptr_t raddr, [[maybe_unused]] void *lbuf, uint32_t sz,
                 uint32_t rkey) const override {
    return AwaitRead{raddr, false};
  }

  AwaitWrite write(uintptr_t raddr, const void *lbuf, uint32_t sz,
                   uint32_t rkey) const override {
    assert(sz <= 64);
    std::memcpy(reinterpret_cast<void *>(raddr), lbuf, sz);
    return AwaitWrite{false};
  }
};

// BEGINS OLD CODE HERE
/* Backend<Threading> defines a backend that simulates context switching
   threads by sleeping before suspending and resuming */
// class Threading {};
// template <>
// class Backend<Threading> {
// private:
//  /* one-way delay of switching to a thread */
//  static constexpr const uint64_t ONEWAY_DELAY_NS = 200;
//
//  OneSidedClient &OSClient;
//  uintptr_t apps_base_laddr;
//  uintptr_t apps_base_raddr;
//  long long oneway_delay_cycles;
//
//  struct AwaitAddrDelayed {
//    uintptr_t addr;
//    long long oneway_delay;
//
//    AwaitAddrDelayed(uintptr_t addr, long long delay)
//        : addr(addr), oneway_delay(delay) {}
//    bool await_ready() { return false; }
//    auto await_suspend(std::coroutine_handle<> coro) {
//      spinloop_cycles(oneway_delay);
//      return true;
//    }
//    void *await_resume() {
//      spinloop_cycles(oneway_delay);
//      return reinterpret_cast<void *>(addr);
//    }
//  };
//
// public:
//  Backend(OneSidedClient &c)
//      : OSClient(c), apps_base_laddr(0), apps_base_raddr(0) {
//    auto cpufreq = get_freq();
//    oneway_delay_cycles = ns_to_cycles(ONEWAY_DELAY_NS, cpufreq);
//    printf("Using threads interleaving RDMA Backend\n");
//    printf("CPU freq=%lld\n", cpufreq);
//    printf("One-way delay in ns=%lu; delay in cycles=%lld\n", ONEWAY_DELAY_NS,
//           oneway_delay_cycles);
//  }
//
//  ~Backend() {}
//
//  void init() {
//    apps_base_laddr = OSClient.get_apps_base_laddr();
//    apps_base_raddr = OSClient.get_apps_base_raddr();
//  }
//
//  auto get_param() noexcept { return AwaitGetParam{}; }
//
//  auto read(uintptr_t raddr, uint32_t sz) noexcept {
//    uintptr_t laddr = apps_base_laddr + (raddr - apps_base_raddr);
//    OSClient.read_async(raddr, laddr, sz);
//    return AwaitAddrDelayed{laddr, oneway_delay_cycles};
//  }
//
//  template <typename T>
//  auto write(uintptr_t raddr, T *data) noexcept {
//    die("not implemented yet");
//    return AwaitVoid<true>{};
//  }
//
//  auto get_baseaddr(uint32_t num_nodes) noexcept {
//    uint32_t next_node = get_next_llnode(num_nodes);
//    return apps_base_raddr + next_node * sizeof(LLNode);
//  }
//
//  uintptr_t get_random_raddr() {
//    die("not implemented yet");
//    return 0;
//  }
//};
//
///* Backend<LocalMemory> defines a DRAM backend that runs coroutines to
// * completion */
// class LocalMemory {};
// template <>
// class Backend<LocalMemory> {
//  char *buffer;
//  LLNode *linkedlist;
//  HugeAllocator huge;
//
//  template <bool suspend>
//  struct AwaitDRAMWrite {
//    void *laddr;
//    void *data;
//    uint32_t sz;
//
//    AwaitDRAMWrite(void *laddr, void *data, uint32_t sz)
//        : laddr(laddr), data(data), sz(sz) {}
//    bool await_ready() { return false; }
//    auto await_suspend(std::coroutine_handle<> coro) {
//      return suspend;  // suspend (true) or not (false)
//    }
//    void await_resume() {
//      // copy the data
//      memcpy(reinterpret_cast<void *>(laddr), data, sz);
//    }
//  };
//
// public:
//#if defined(WORKLOAD_HASHTABLE)
//  struct cuckoo_hash table;
//#endif
//
//  Backend(OneSidedClient &c) : buffer(nullptr), linkedlist(nullptr) {
//    buffer = huge.get();
//    printf("Using local DRAM Backend\n");
//  }
//
//  ~Backend() { destroy_linkedlist(linkedlist); }
//
//  void init() {
//#if defined(WORKLOAD_HASHTABLE)
//    // no need to destroy the table since we are giving it preallocated memory
//    cuckoo_hash_init(&table, 16, static_cast<void *>(buffer));
//#else
//    linkedlist = create_linkedlist<LLNode>(buffer, RMCK_APPS_BUFF_SZ);
//#endif
//  }
//
//  auto get_param() noexcept { return AwaitGetParam{}; }
//
//  auto read(uintptr_t addr, uint32_t sz) noexcept {
//    // interleaved: uncomment next
//    for (auto cl = 0u; cl < sz; cl += 64)
//      __builtin_prefetch(reinterpret_cast<void *>(addr + cl), 0, 0);
//    return AwaitAddr<true>{addr};
//    // run to completion: uncomment next
//    // return AwaitAddr<false>{addr};
//  }
//
//  // with local memory raddr=laddr
//  auto read_laddr(uintptr_t laddr, uint32_t sz) noexcept {
//    return read(laddr, sz);
//  }
//
//  auto write_raddr(uintptr_t laddr, void *data, uint32_t sz) noexcept {
//    die("not implemented yet");
//    return AwaitVoid<false>{};
//  }
//
//  auto write_laddr(uintptr_t laddr, void *data, uint32_t sz) noexcept {
//    assert(sz <= 64);
//
//    void *addr = reinterpret_cast<void *>(laddr);
//    // interleaved: uncomment next
//    __builtin_prefetch(addr, 1, 0);
//    return AwaitDRAMWrite<true>{addr, data, sz};
//    // run to completion: uncomment next
//    // return AwaitDRAMWrite<false>{addr, data, sz};
//  }
//
//  auto get_baseaddr(uint32_t num_nodes) noexcept {
//    uint32_t next_node = get_next_llnode(num_nodes);
//    return reinterpret_cast<uintptr_t>(buffer + next_node * sizeof(LLNode));
//  }
//
//  uintptr_t get_random_raddr() {
//    die("not implemented yet");
//    return 0;
//  }
//};

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
 * memory
 * TODO: lock/unlock should be part of backends? */
class RMCLock {
 public:
  RMCLock() {
    if (pthread_spin_init(&l, PTHREAD_PROCESS_PRIVATE) != 0)
      die("could not init spin lock\n");
  }

  ~RMCLock() { pthread_spin_destroy(&l); }
  RMCLock(const RMCLock &) = delete;             // copy constructor
  RMCLock(RMCLock &&) = delete;                  // move constructor
  RMCLock &operator=(const RMCLock &) = delete;  // copy assignment operator
  RMCLock &operator=(RMCLock &&) = delete;       // move assignment operator

  /* Need to return a CoroRMC because if we cannot take the lock, we need
   * to suspend here (and later return here to retry) */
  CoroRMC lock(const BackendBase *b) {
    while (pthread_spin_trylock(&l) != 0) co_await std::suspend_always{};
    IgnoreReply ignore;
    co_return &ignore;
  }

  /* TODO: This might not need to return a CoroRMC? */
  CoroRMC unlock(const BackendBase *b) {
    pthread_spin_unlock(&l);
    co_await std::suspend_never{};
    IgnoreReply ignore;
    co_return &ignore;
  }

 private:
  pthread_spinlock_t l;
};
#endif
