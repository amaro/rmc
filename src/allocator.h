#pragma once

#include <forward_list>
#include <stdlib.h>

#include "rdma/rdmapeer.h"
#include "utils/utils.h"

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
