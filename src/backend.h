#pragma once

#include <cstring>

#include "corormc.h"
#include "onesidedclient.h"
#include "rdma/rdmapeer.h"
#include "rmcs.h"

constexpr uint16_t DRAM_MAX_SIZE_MULTIOP = 8;

struct AwaitRead {
  bool should_suspend = false;
  uintptr_t raddr = 0;
  void *lbuf = nullptr;
  // to handle continuations (e.g., locks)
  CoroRMC::promise_type *promise = nullptr;
  size_t memcpy_sz = 0;

  constexpr bool await_ready() { return false; }

  auto await_suspend(std::coroutine_handle<CoroRMC::promise_type> coro) {
    promise = &coro.promise();

    // this is a read coming from a nested CoroRMC, need the caller's
    // promise
    if (promise->continuation) promise = &promise->continuation.promise();

    promise->waiting_mem_access = true;
    return should_suspend;  // true = suspend; false = don't
  }

  void await_resume() {
    promise->waiting_mem_access = false;
    /* if true, we are using the PrefetchDRAM backend and we need to memcpy
       before resuming */
    if (memcpy_sz != 0)
      std::memcpy(lbuf, reinterpret_cast<void *>(raddr), memcpy_sz);
  }
};

struct AwaitWrite {
  bool should_suspend = false;
  uintptr_t raddr = 0;
  const void *lbuf = nullptr;
  CoroRMC::promise_type *promise = nullptr;
  size_t memcpy_sz = 0;

  constexpr bool await_ready() { return false; }
  auto await_suspend(std::coroutine_handle<CoroRMC::promise_type> coro) {
    promise = &coro.promise();

    // this is a write coming from a nested CoroRMC, need the caller's
    // promise
    if (promise->continuation) promise = &promise->continuation.promise();

    promise->waiting_mem_access = true;
    return should_suspend;  // true = suspend; false = don't
  }
  constexpr void await_resume() {
    promise->waiting_mem_access = false;
    /* if true, we are using the PrefetchDRAM backend and we need to memcpy
       before resuming */
    if (memcpy_sz != 0)
      std::memcpy(reinterpret_cast<void *>(raddr), lbuf, memcpy_sz);
  }
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
  virtual void read_nosuspend(uintptr_t raddr, void *lbuf, uint32_t sz,
                              uint32_t rkey) const = 0;
  virtual AwaitWrite write(uintptr_t raddr, const void *data, uint32_t sz,
                           uint32_t rkey) const = 0;
  virtual AwaitRead cmp_swp(uintptr_t raddr, uint64_t &llock, uint64_t cmp,
                            uint64_t swp, uint32_t rkey) const = 0;
  virtual uint16_t free_slots() const = 0;
};

/* Cooperative multi tasking RDMA backend */
class CoopRDMA : public BackendBase {
  using OneSidedOp = RDMAContext::OneSidedOp;

 public:
  CoopRDMA(OneSidedClient &c) : BackendBase(c) {
    puts("BACKEND: Cooperative RDMA (default)");
  }

  void init() {}

  AwaitRead read(uintptr_t raddr, void *lbuf, uint32_t sz,
                 uint32_t rkey) const final {
    assert(raddr != 0);
    const uintptr_t laddr = reinterpret_cast<uintptr_t>(lbuf);

    OSClient.post_op_from_frame(OneSidedOp{.raddr = raddr,
                                           .laddr = laddr,
                                           .len = sz,
                                           .rkey = rkey,
                                           .cmp = 0,
                                           .swp = 0,
                                           .optype = OneSidedOp::OpType::READ});

    return AwaitRead{.should_suspend = true};
  }

  void read_nosuspend(uintptr_t raddr, void *lbuf, uint32_t sz,
                      uint32_t rkey) const final {
    assert(raddr != 0);
    const uintptr_t laddr = reinterpret_cast<uintptr_t>(lbuf);

    OSClient.post_op_from_frame(OneSidedOp{.raddr = raddr,
                                           .laddr = laddr,
                                           .len = sz,
                                           .rkey = rkey,
                                           .cmp = 0,
                                           .swp = 0,
                                           .optype = OneSidedOp::OpType::READ});
  }

  AwaitWrite write(uintptr_t raddr, const void *lbuf, uint32_t sz,
                   uint32_t rkey) const final {
    const uintptr_t laddr = reinterpret_cast<uintptr_t>(lbuf);

    OSClient.post_op_from_frame(
        OneSidedOp{.raddr = raddr,
                   .laddr = laddr,
                   .len = sz,
                   .rkey = rkey,
                   .cmp = 0,
                   .swp = 0,
                   .optype = OneSidedOp::OpType::WRITE});
    return AwaitWrite{.should_suspend = true};
  }

  AwaitRead cmp_swp(uintptr_t raddr, uint64_t &llock, uint64_t cmp,
                    uint64_t swp, uint32_t rkey) const final {
    OSClient.post_op_from_frame(
        OneSidedOp{.raddr = raddr,
                   .laddr = reinterpret_cast<uintptr_t>(&llock),
                   .len = 8,
                   .rkey = rkey,
                   .cmp = cmp,
                   .swp = swp,
                   .optype = OneSidedOp::OpType::CMP_SWP});

    return AwaitRead{.should_suspend = true};
  }

  virtual uint16_t free_slots() const final { return OSClient.free_slots(); }
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
    puts("BACKEND: run-to-completion RDMA");
  }

  void init() {
    ctx = &rclient.get_context(0);
    send_cq = rclient.get_send_cq(0);
  }

  AwaitRead read(uintptr_t raddr, void *lbuf, uint32_t sz,
                 uint32_t rkey) const final {
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
    return AwaitRead{.should_suspend = false};
  }

  void read_nosuspend(uintptr_t raddr, void *lbuf, uint32_t sz,
                      uint32_t rkey) const final {
    die("not supported yet");
  }

  AwaitWrite write(uintptr_t raddr, const void *lbuf, uint32_t sz,
                   uint32_t rkey) const final {
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

    return AwaitWrite{.should_suspend = false};
  }

  AwaitRead cmp_swp(uintptr_t raddr, uint64_t &llock, uint64_t cmp,
                    uint64_t swp, uint32_t rkey) const final {
    rclient.start_batched_ops(ctx);
    OSClient.post_op_from_frame(
        OneSidedOp{.raddr = raddr,
                   .laddr = reinterpret_cast<uintptr_t>(&llock),
                   .len = 8,
                   .rkey = rkey,
                   .cmp = cmp,
                   .swp = swp,
                   .optype = OneSidedOp::OpType::CMP_SWP});
    rclient.end_batched_ops();
    rclient.poll_atleast(1, send_cq, [](size_t) constexpr->void{});

    return AwaitRead{.should_suspend = false};
  }

  virtual uint16_t free_slots() const final {
    /* since there can't be anything else in the pipe */
    return QP_MAX_1SIDED_WRS;
  }
};

inline AwaitRead dram_cmp_swp(uintptr_t raddr, uint64_t &llock, uint64_t cmp,
                              uint64_t swp, uint32_t rkey) {
  uint64_t &ref_raddr = *(reinterpret_cast<uint64_t *>(raddr));
  std::atomic_ref<uint64_t> atomic_raddr{ref_raddr};
  /* (atomically)
     if *ptr_raddr == cmp:
       *ptr_raddr = swp
       return true
     else:
       cmp = *ptr_raddr
       return false
  */
  atomic_raddr.compare_exchange_strong(cmp, swp);
  /* the following is enough in case we exchanged value or not.
     if we exchanged the value, llock must have been equal to cmp.
     if we didn't exchange the value, cmp has original value */
  llock = cmp;

  return AwaitRead{.should_suspend = false};
}

/* Backend for server location. A list of RMCs issue prefetch instructions for
 * their accesses and immediately suspend, giving time for cpu prefetchers to
 * get data closer. After all RMCs in the list prefetch, we resume them in
 * order, and actually access their data */
class PrefetchDRAM : public BackendBase {
  DramMrAllocator *_dram_allocator;
  std::vector<MemoryRegion> *_mrs;

 public:
  PrefetchDRAM(OneSidedClient &c) : BackendBase(c) {
    puts("BACKEND: prefetching DRAM");
  }

  void init(DramMrAllocator *allocator, std::vector<MemoryRegion> *mrs) {
    _dram_allocator = allocator;
    _mrs = mrs;
  }

  AwaitRead read(uintptr_t raddr, void *lbuf, uint32_t sz,
                 uint32_t rkey) const final {
    for (auto cl = 0u; cl < sz; cl += 64)
      __builtin_prefetch(reinterpret_cast<void *>(raddr + cl), 0, 3);

    return AwaitRead{
        .should_suspend = true, .raddr = raddr, .lbuf = lbuf, .memcpy_sz = sz};
  }

  void read_nosuspend(uintptr_t raddr, void *lbuf, uint32_t sz,
                      uint32_t rkey) const final {
    die("not supported yet");
  }

  AwaitWrite write(uintptr_t raddr, const void *lbuf, uint32_t sz,
                   uint32_t rkey) const final {
    for (auto cl = 0u; cl < sz; cl += 64)
      __builtin_prefetch(reinterpret_cast<void *>(raddr + cl), 1, 3);

    return AwaitWrite{
        .should_suspend = true, .raddr = raddr, .lbuf = lbuf, .memcpy_sz = sz};
  }

  /* cmp_swp runs to completion because we don't want to suspend before taking
   * a lock */
  AwaitRead cmp_swp(uintptr_t raddr, uint64_t &llock, uint64_t cmp,
                    uint64_t swp, uint32_t rkey) const final {
    return dram_cmp_swp(raddr, llock, cmp, swp, rkey);
  }

  /* TODO: figure out what to use here */
  virtual uint16_t free_slots() const final { return DRAM_MAX_SIZE_MULTIOP; }
};

class CompDRAM : public BackendBase {
  DramMrAllocator *_dram_allocator;
  std::vector<MemoryRegion> *_mrs;

 public:
  CompDRAM(OneSidedClient &c) : BackendBase(c) {
    puts("BACKEND: run to completion DRAM");
  }

  void init(DramMrAllocator *allocator, std::vector<MemoryRegion> *mrs) {
    _dram_allocator = allocator;
    _mrs = mrs;
  }

  AwaitRead read(uintptr_t raddr, void *lbuf, uint32_t sz,
                 uint32_t rkey) const final {
    std::memcpy(lbuf, reinterpret_cast<void *>(raddr), sz);
    return AwaitRead{.should_suspend = false};
  }

  void read_nosuspend(uintptr_t raddr, void *lbuf, uint32_t sz,
                      uint32_t rkey) const final {
    die("not supported yet");
  }

  AwaitWrite write(uintptr_t raddr, const void *lbuf, uint32_t sz,
                   uint32_t rkey) const final {
    std::memcpy(reinterpret_cast<void *>(raddr), lbuf, sz);
    return AwaitWrite{.should_suspend = false};
  }

  AwaitRead cmp_swp(uintptr_t raddr, uint64_t &llock, uint64_t cmp,
                    uint64_t swp, uint32_t rkey) const final {
    return dram_cmp_swp(raddr, llock, cmp, swp, rkey);
  }

  /* TODO: figure out what to use here */
  virtual uint16_t free_slots() const final { return DRAM_MAX_SIZE_MULTIOP; }
};

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
