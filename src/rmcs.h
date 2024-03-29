#pragma once

#include <array>
#include <atomic>

#include "allocator.h"
#include "backend.h"
#include "config.h"

enum class RMCType : int {
  TRAVERSE_LL,
  MULTI_TRAVERSE_LL,
  LOCK_TRAVERSE_LL,
  UPDATE_LL,
  KVSTORE,
  TAO
};
struct RMCBase;
using AppAddr = uintptr_t;

template <typename T>
struct AppPtr {
  const BackendBase *b;
  AppAddr raddr;
  uint32_t rkey;
  T lbuf;

  AppPtr(const BackendBase *b, AppAddr raddr, uint32_t rkey)
      : b(b), raddr(raddr), rkey(rkey) {}

  [[nodiscard]] T &get_ref() { return lbuf; }

  void set_raddr(AppAddr raddr) { this->raddr = raddr; }

  AwaitRead read() { return b->read(raddr, &lbuf, sizeof(T), rkey); }

  AwaitWrite write() const { return b->write(raddr, &lbuf, sizeof(T), rkey); }

  /* assumes raddr points to T &array[0] */
  AppAddr raddr_for_index(size_t index) { return raddr + index * sizeof(T); }

  void read_nosuspend() { b->read_nosuspend(raddr, &lbuf, sizeof(T), rkey); }
};

class RMCLock {
  AppAddr raddr;
  uint32_t rkey;
  /* TODO: this should be made thread safe because RMCLock can be called from
     multiple threads */
  std::queue<std::coroutine_handle<CoroRMC::promise_type>> blockedq;
  std::deque<std::coroutine_handle<>> *runqueue = nullptr;

 public:
  void init_runtime(AppAddr raddr, uint32_t rkey,
                    std::deque<std::coroutine_handle<>> *runqueue) {
    this->raddr = raddr;
    this->rkey = rkey;
    this->runqueue = runqueue;
  }

  /* write a 0 to addr */
  void init_server(void *addr) { *(static_cast<uint64_t *>(addr)) = 0; }

  /* llock comes from a coro frame, so it is unique for every caller regardless
   * of number of threads being used.
     TODO: make this accept a AppPtr<uint64_t>& */
  CoroRMC lock(const BackendBase *b, uint64_t &llock) {
    auto handle = co_await GetHandle{};

    assert(handle.promise().blocked == false);
    /* set the local lock to 1 */
    llock = 1;

    /* if *raddr is 0 then atomically set it to 1, also set *llock to the
     * original value of *raddr */
    co_await b->cmp_swp(raddr, llock, 0, 1, rkey);

    /* if llock == 0 then we have acquired the lock */
    while (llock != 0) {
      if (!handle.promise().blocked) {
        /* if a coro is blocked, it won't be scheduled back */
        handle.promise().blocked = true;
        /* add handle to blocked queue */
        blockedq.push(handle);
      }

      /* suspend this coro until another coro unlock()s */
      co_await std::suspend_always{};

      /* suspended coros resume here; retry taking the lock */
      co_await b->cmp_swp(raddr, llock, 0, 1, rkey);
    }

    assert(handle.promise().blocked == false);
    /* lock has been taken, return to caller */
  }

  CoroRMC unlock(const BackendBase *b, uint64_t &llock) {
    /* set local lock to 0 */
    llock = 0;
    /* write local lock value to raddr
       TODO: I think this can be a write_nowait() */
    co_await b->write(raddr, &llock, sizeof(llock), rkey);

    if (!blockedq.empty()) {
      std::coroutine_handle<CoroRMC::promise_type> rmc = blockedq.front();
      blockedq.pop();
      /* unblocked coros will be eventually resumed by scheduler */
      rmc.promise().blocked = false;
      /* the continuation is the root rmc, which is what we keep in runqueue */
      runqueue->push_back(rmc.promise().continuation);
    }
  }
};

class RMCBase {
 public:
  /* Runs at the runtime location, main handler for this RMC */
  virtual CoroRMC runtime_handler(const BackendBase *b, const ExecReq *req) = 0;
  /* Runs at the runtime location. Receives memory region information such that
     we can cache it at the class level, or return if appropriate. */
  virtual CoroRMC runtime_init(
      const MemoryRegion &mr,
      std::deque<std::coroutine_handle<>> *runqueue) = 0;
  /* Runs at the server: requests memory, and gets access to it, so
     it can be initialized. */
  virtual MemoryRegion server_init(MrAllocator &sa) = 0;

  RMCBase() = default;
  virtual ~RMCBase() = default;

  RMCBase(const RMCBase &) = delete;             // copy constructor
  RMCBase(RMCBase &&) = delete;                  // move constructor
  RMCBase &operator=(const RMCBase &) = delete;  // copy assignment operator
  RMCBase &operator=(RMCBase &&) = delete;       // move assignment operator

 protected:
  template <int N, typename T>
  CoroRMC multiread(const BackendBase *b, std::array<AppPtr<T>, N> &ptrs) {
    int issued = 0;
    auto handle = co_await GetHandle{};
    /* if we haven't submitted N reads, try again */
    while (issued < N) {
      int free_slots = b->free_slots();
      /* How many reads can we issue at this time. We still need to issue N -
       * issued */
      int issue_slots = std::min(free_slots, N - issued);
      int i = 0;

      for (; i < issue_slots - 1; i++) ptrs[issued + i].read_nosuspend();

      handle.promise().multi_ops += issue_slots;
      /* the last read we issue from a multiop must suspend */
      co_await ptrs[issued + i].read();
      issued += issue_slots;
    }
    assert(handle.promise().multi_ops == 0);
  }
};

CoroRMC rmcs_get_init(RMCType type, const MemoryRegion &mr,
                      std::deque<std::coroutine_handle<>> *runqueue);
CoroRMC rmcs_get_handler(const RMCType type, const BackendBase *b,
                         const ExecReq *req);
void rmcs_server_init(MrAllocator &sa, std::vector<MemoryRegion> &allocs);
