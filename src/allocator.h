#pragma once

#include "utils/utils.h"
#include <stdlib.h>
#include <sys/mman.h>

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
  /* 1 GB */
  static constexpr size_t HUGE_PAGE_SIZE = 1 << 30;

  char *ptr;

  HugeAllocator() {
    ptr = static_cast<char *>(
        mmap(nullptr, HUGE_PAGE_SIZE, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0));
    if (ptr == MAP_FAILED)
      DIE("huge allocation failed");
  }

  ~HugeAllocator() { munmap(ptr, HUGE_PAGE_SIZE); }

  char *get() { return ptr; }

  constexpr size_t size() { return HUGE_PAGE_SIZE; }
};
