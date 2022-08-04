#pragma once

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
