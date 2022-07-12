#pragma once

#include <sys/mman.h>

#include <cstdlib>

#include "rdma/rdmaserver.h"
#include "utils/utils.h"

static constexpr size_t HUGE_PAGE_SIZE = 1U << 30;  // 1 GB
static constexpr size_t CACHELINE_SIZE = 64;

struct HugeAllocator {
 private:
  char *ptr;

 public:
  HugeAllocator() {
    ptr = static_cast<char *>(
        mmap(nullptr, HUGE_PAGE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC,
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

struct RMCAllocator {
 private:
  struct header {
    header *next;
    size_t size;
  };

  struct thread_alloc {
    header *free_root;
    uintptr_t alloc_offset;
    size_t remaining;
  };

  HugeAllocator huge;
  std::vector<thread_alloc> allocs;

  auto _alloc(thread_alloc &alloc, size_t sz) -> auto * {
    /* can we reuse prev allocation */
    if (alloc.free_root != nullptr && sz <= alloc.free_root->size) {
      void *mem = alloc.free_root;
      alloc.free_root = alloc.free_root->next;
      return mem;
    }

    alloc.remaining -= sz;
    assert(alloc.remaining > 0);
    auto *mem = reinterpret_cast<void *>(alloc.alloc_offset);
    alloc.alloc_offset += sz;
    return mem;
  }

  auto _free(thread_alloc &alloc, void *p, size_t sz) {
    auto *new_entry = static_cast<header *>(p);
    new_entry->next = alloc.free_root;
    new_entry->size = sz;
    alloc.free_root = new_entry;
  }

 public:
  RMCAllocator() = default;

  auto init(uint16_t num_threads) -> auto {
    auto size_per_thread = huge.size() / num_threads;
    auto curr_offset = reinterpret_cast<uintptr_t>(huge.get());

    printf("RMCAllocator: init for %u threads\n", num_threads);
    for (auto i = 0U; i < num_threads; ++i) {
      allocs.emplace_back(thread_alloc{.free_root = nullptr,
                                       .alloc_offset = curr_offset,
                                       .remaining = size_per_thread});
      curr_offset += size_per_thread;
    }
  }

  auto alloc(uint16_t tid, size_t sz) -> auto * {
    assert(tid < allocs.size());
    return _alloc(allocs[tid], sz);
  }

  void free(uint16_t tid, void *p, size_t sz) {
    assert(tid < allocs.size());
    return _free(allocs[tid], p, sz);
  }

  auto &get_huge_buffer() { return huge; }
};

inline RMCAllocator &get_frame_alloc() {
  static RMCAllocator allocator;
  return allocator;
}

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
