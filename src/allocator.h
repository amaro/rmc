#pragma once

#include <stdlib.h>
#include <sys/mman.h>

#include "rdma/rdmaserver.h"
#include "utils/utils.h"

static constexpr size_t HUGE_PAGE_SIZE = 1 << 30;  // 1 GB

struct RMCAllocator {
  struct header {
    header *next;
    size_t size;
  };

  header *root = nullptr;

  ~RMCAllocator() {
    auto current = root;
    while (current) {
      auto next = current->next;
      ::free(current);
      current = next;
    }
  }

  void *alloc(size_t sz) {
    /* can we reuse prev allocation */
    if (root && sz <= root->size) {
      void *mem = root;
      root = root->next;
      return mem;
    }

    return aligned_alloc(64, sz);
  }

  void free(void *p, size_t sz) {
    auto new_entry = static_cast<header *>(p);
    new_entry->next = root;
    new_entry->size = sz;
    root = new_entry;
  }
};

struct HugeAllocator {
  char *ptr;

  HugeAllocator() {
    ptr = static_cast<char *>(
        mmap(nullptr, HUGE_PAGE_SIZE, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0));
    rt_assert(ptr != MAP_FAILED, "huge allocation failed");
    // std::memset(ptr, 0, HUGE_PAGE_SIZE);
  }

  HugeAllocator(const HugeAllocator &) = delete;
  HugeAllocator(HugeAllocator &&source) : ptr(source.ptr) {
    source.ptr = nullptr;
  }

  ~HugeAllocator() {
    if (ptr) munmap(ptr, HUGE_PAGE_SIZE);
  }

  char *get() { return ptr; }

  constexpr size_t size() { return HUGE_PAGE_SIZE; }
};

struct ServerAlloc {
  void *addr;
  size_t length;
  const ibv_mr *mr;
};

/* Used by RMCs to allocate server memory and register with RDMA to make it
 * available to nicserver's onesidedclient */
class ServerAllocator {
  static constexpr uint8_t MAX_SERVER_ALLOCS = 8;

  RDMAServer &rserver;
  std::vector<HugeAllocator> buffers;
  HugeAllocator &huge;

 public:
  ServerAllocator(RDMAServer &rserver, HugeAllocator &huge)
      : rserver(rserver), huge(huge) {}

  /* TODO: don't allocate more than requested memory? */
  ServerAlloc request_memory(size_t sz) {
    rt_assert(buffers.size() <= MAX_SERVER_ALLOCS,
              "too many server allocations");
    rt_assert(sz <= HUGE_PAGE_SIZE, "server memory sz too large");

    HugeAllocator hugealloc;

    char *addr = hugealloc.get();
    const ibv_mr *mr = rserver.register_mr(
        addr, sz,
        IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE |
            IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_ATOMIC |
            IBV_ACCESS_RELAXED_ORDERING);
    printf("rdma_mr rkey=%u\n", mr->rkey);

    buffers.push_back(std::move(hugealloc));
    return ServerAlloc{addr, sz, mr};
  }
};
