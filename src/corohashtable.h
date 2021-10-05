#pragma once

#include "corormc.h"
#include "lib/cuckoo_hash.h"

static inline struct cuckoo_hash table;
// in a better implementation, keys come from requests
inline const std::vector<int> keys{1, 2, 3, 4, 5, 6, 8, 9, 10};

static void __attribute__((constructor)) init_table() {
  cuckoo_hash_init(&table, 16);
}

static void __attribute__((destructor)) destroy_table() {
  cuckoo_hash_destroy(&table);
}

struct data {
  uint64_t val1;
  uint64_t val2;
};

template <class T> static inline void *get_next_raddr(Backend<T> &b) {
  static std::atomic<uint32_t> raddr_given = 0;

  void *raddr =
      reinterpret_cast<void *>(b.get_base_raddr() + raddr_given * sizeof(data));
  raddr_given++;
  return raddr;
}

template <class T> inline CoroRMC hash_insert(Backend<T> &b) {
  struct cuckoo_hash_item *item;

  if (!item)
    co_yield 1;
  else
    co_yield 0;
}

template <class T> inline CoroRMC hash_query(Backend<T> &b) {
  thread_local uint64_t read_idx = 0;
  struct cuckoo_hash_item *item;
  struct data *readdata = nullptr;

  read_lock();
  item = cuckoo_hash_lookup(&table, &keys[read_idx], sizeof(struct data));
  if (item) {
    readdata = static_cast<struct data *>(co_await b.read(
        reinterpret_cast<uintptr_t>(item->value), sizeof(struct data)));
    std::cout << "read val1=" << readdata->val1 << "\n";
  }

  read_unlock();

  inc_with_wraparound(read_idx, keys.size());

  if (item)
    co_yield 1;
  else
    co_yield 0;
}
