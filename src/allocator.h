#pragma once

#include <sys/mman.h>

#include <cstdlib>

#include "rdma/rdmaserver.h"
#include "utils/utils.h"

static constexpr size_t HUGE_PAGE_SIZE = 1U << 30;  // 1 GB
static constexpr size_t CACHELINE_SIZE = 64;

struct RMCAllocator {
 private:
  struct header {
    header *next;
    size_t size;
  };

  header *root = nullptr;

 public:
  RMCAllocator() = default;

  ~RMCAllocator() {
    auto *current = root;
    while (current != nullptr) {
      auto *next = current->next;
      ::free(current);
      current = next;
    }
  }

  RMCAllocator(const RMCAllocator &) = delete;
  RMCAllocator(RMCAllocator &&) = delete;
  RMCAllocator &operator=(const RMCAllocator &) = delete;
  RMCAllocator &operator=(RMCAllocator &&) = delete;

  auto alloc(size_t sz) -> auto * {
    /* can we reuse prev allocation */
    if (root != nullptr && sz <= root->size) {
      void *mem = root;
      root = root->next;
      return mem;
    }

    return aligned_alloc(CACHELINE_SIZE, sz);
  }

  void free(void *p, size_t sz) {
    auto *new_entry = static_cast<header *>(p);
    new_entry->next = root;
    new_entry->size = sz;
    root = new_entry;
  }
};

struct HugeAllocator {
 private:
  char *ptr;

 public:
  HugeAllocator() {
    ptr = static_cast<char *>(
        mmap(nullptr, HUGE_PAGE_SIZE, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0));
    rt_assert(ptr != MAP_FAILED, "huge allocation failed");
    // std::memset(ptr, 0, HUGE_PAGE_SIZE);
  }

  ~HugeAllocator() {
    if (ptr) munmap(ptr, HUGE_PAGE_SIZE);
  }

  HugeAllocator(HugeAllocator &&source) noexcept : ptr(source.ptr) {
    source.ptr = nullptr;
  }

  HugeAllocator(const HugeAllocator &) = delete;
  HugeAllocator &operator=(const HugeAllocator &) = delete;
  HugeAllocator &operator=(HugeAllocator &&) = delete;

  auto get() -> auto * { return ptr; }

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

 public:
  explicit ServerAllocator(RDMAServer &rserver) : rserver(rserver) {}

  /* TODO: don't allocate more than requested memory? */
  auto request_memory(size_t sz) -> ServerAlloc {
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
    buffers.push_back(std::move(hugealloc));
    return ServerAlloc{addr, sz, mr};
  }
};
