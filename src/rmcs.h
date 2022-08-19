#pragma once

#include <atomic>

#include "allocator.h"
#include "backend.h"
#include "config.h"

enum class RMCType : int { TRAVERSE_LL, LOCK_TRAVERSE_LL, UPDATE_LL, KVSTORE };

struct RMCBase;
using RemoteAddr = uintptr_t;

template <typename T>
struct RemotePtr {
  const BackendBase *b;
  RemoteAddr raddr;
  uint32_t rkey;
  T lbuf;

  RemotePtr(const BackendBase *b, RemoteAddr raddr, uint32_t rkey)
      : b(b), raddr(raddr), rkey(rkey) {}

  [[nodiscard]] T &get_ref() { return lbuf; }

  void set_raddr(RemoteAddr raddr) { this->raddr = raddr; }

  AwaitRead read() { return b->read(raddr, &lbuf, sizeof(T), rkey); }

  AwaitWrite write() const { return b->write(raddr, &lbuf, sizeof(T), rkey); }

  /* assumes raddr points to T &array[0] */
  RemoteAddr raddr_for_index(size_t index) { return raddr + index * sizeof(T); }
};

class RMCLock {
  RemoteAddr raddr;
  uint32_t rkey;
  /* TODO: this should be made thread safe because RMCLock can be called from
     multiple threads */
  std::queue<std::coroutine_handle<CoroRMC::promise_type>> blockedq;

 public:
  void init(RemoteAddr raddr, uint32_t rkey) {
    this->raddr = raddr;
    this->rkey = rkey;
  }

  /* llock comes from a coro frame, so it is unique for every caller regardless
   * of number of threads being used. */
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
    }
  }
};

struct RMCBase {
  /* Runs at the runtime location, main handler for this RMC */
  virtual CoroRMC runtime_handler(const BackendBase *b, const ExecReq *req) = 0;
  /* Runs at the runtime location. Receives memory region information such that
     we can cache it at the class level, or return if appropriate. */
  virtual CoroRMC runtime_init(const MemoryRegion &mr) = 0;
  /* Runs at the server: requests memory, and gets access to it, so
     it can be initialized. */
  virtual MemoryRegion server_init(MrAllocator &sa) = 0;

  RMCBase() = default;
  virtual ~RMCBase() = default;

  RMCBase(const RMCBase &) = delete;             // copy constructor
  RMCBase(RMCBase &&) = delete;                  // move constructor
  RMCBase &operator=(const RMCBase &) = delete;  // copy assignment operator
  RMCBase &operator=(RMCBase &&) = delete;       // move assignment operator
};

CoroRMC rmcs_get_init(RMCType type, const MemoryRegion &mr);
CoroRMC rmcs_get_handler(const RMCType type, const BackendBase *b,
                         const ExecReq *req);
void rmcs_server_init(MrAllocator &sa, std::vector<MemoryRegion> &allocs);
