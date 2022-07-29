#pragma once

#include "allocator.h"
#include "backend.h"
#include "config.h"

enum class RMCType : int {
  TRAVERSE_LL,
  LOCK_TRAVERSE_LL,
  UPDATE_LL,
  HASHTABLE
};

struct RMCBase;
using RemoteAddr = uintptr_t;

template <typename T>
struct RemotePtr {
  friend RMCBase;
  T *rptr;
  T lbuf;

  RemotePtr(RemoteAddr raddr) : rptr(reinterpret_cast<T *>(raddr)) {}

  [[nodiscard]] T &get() { return lbuf; }

  void setptr(T *ptr) { rptr = ptr; }
};

struct RMCBase {
  /* Runs at the runtime location, main handler for this RMC */
  virtual CoroRMC runtime_handler(const BackendBase *b) = 0;
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

  template <typename T>
  AwaitRead read(const BackendBase *b, RemotePtr<T> &ptr, uint32_t rkey) const {
    return b->read(reinterpret_cast<RemoteAddr>(ptr.rptr), &ptr.lbuf, sizeof(T),
                   rkey);
  }

  template <typename T>
  AwaitWrite write(const BackendBase *b, RemotePtr<T> &ptr,
                   uint32_t rkey) const {
    return b->write(reinterpret_cast<RemoteAddr>(ptr.rptr), &ptr.lbuf,
                    sizeof(T), rkey);
  }
};

CoroRMC rmcs_get_init(RMCType type, const MemoryRegion &mr);
CoroRMC rmcs_get_handler(const RMCType type, const BackendBase *b);
void rmcs_server_init(MrAllocator &sa, std::vector<MemoryRegion> &allocs);
