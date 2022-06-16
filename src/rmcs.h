#pragma once

#include "backend.h"
#include "config.h"

enum RMCType : int { TRAVERSE_LL, LOCK_TRAVERSE_LL, RANDOM_WRITES, HASHTABLE };

class RMCTraverseLL {
  bool inited = false;

 public:
  template <class T>
  CoroRMC handler(Backend<T> &b) {
    int num_nodes = co_await b.get_param();
    uintptr_t addr = b.get_baseaddr(num_nodes);
    LLNode *node = nullptr;

    for (int i = 0; i < num_nodes; ++i) {
      node = static_cast<LLNode *>(co_await b.read(addr, sizeof(LLNode)));
      addr = reinterpret_cast<uintptr_t>(node->next);
    }

    co_yield 1;
  }

  template <class T>
  void init(Backend<T> &b) {
    inited = true;
  }

  template <class T>
  void exit(Backend<T> &b) {
    inited = false;
  }

  bool is_inited() { return inited; }
};

class RMCLockTraverseLL {
  bool inited = false;
  RMCLock rmclock;

 public:
  template <class T>
  CoroRMC handler(Backend<T> &b) {
    int num_nodes = co_await b.get_param();
    uintptr_t addr = b.get_baseaddr(num_nodes);
    LLNode *node = nullptr;

    for (int i = 0; i < num_nodes; ++i) {
      co_await rmclock.lock(b);
      node = static_cast<LLNode *>(co_await b.read(addr, sizeof(LLNode)));
      addr = reinterpret_cast<uintptr_t>(node->next);
      co_await rmclock.unlock(b);
    }

    co_yield 1;
  }

  template <class T>
  void init(Backend<T> &b) {
    inited = true;
  }

  template <class T>
  void exit(Backend<T> &b) {
    inited = false;
  }

  bool is_inited() { return inited; }
};

class RMCRandomWrites {
  bool inited = false;

 public:
  template <class T>
  CoroRMC handler(Backend<T> &b) {
    const uint32_t num_writes = co_await b.get_param();
    uint64_t val = 0xDEADBEEF;

    for (auto i = 0u; i < num_writes; ++i) {
      co_await b.write_raddr(b.get_random_raddr(), &val, sizeof(val));
    }

    co_yield 1;
  }

  template <class T>
  void init(Backend<T> &b) {
    inited = true;
  }

  template <class T>
  void exit(Backend<T> &b) {
    inited = false;
  }

  bool is_inited() { return inited; }
};

inline static RMCTraverseLL traversell;
inline static RMCLockTraverseLL locktraversell;
inline static RMCRandomWrites randomwrites;

#ifdef WORKLOAD_HASHTABLE
#include "lib/cuckoo_hash.h"

inline static RMCLock rmclock;

// in an actual implementation, keys come from requests
inline const std::vector<int> KEYS{1, 2, 3, 4, 5, 6, 8, 9, 10};
constexpr uint64_t KEY_LEN = sizeof(int);

template <class T>
inline CoroRMC lookup(Backend<T> &b, const struct cuckoo_hash *hash,
                      const void *key, size_t key_len, uint32_t h1, uint32_t h2,
                      struct cuckoo_hash_item **res) {
  uint32_t mask = (1U << hash->power) - 1;

  struct _cuckoo_hash_elem *elem, *end;

  // orig code: elem = bin_at(hash, (h1 & mask));
  // read the data from HM the while loop below will access
  elem = static_cast<_cuckoo_hash_elem *>(co_await b.read_laddr(
      reinterpret_cast<uintptr_t>(bin_at(hash, (h1 & mask))),
      sizeof(struct _cuckoo_hash_elem) * hash->bin_size));
  end = elem + hash->bin_size;
  while (elem != end) {
    if (elem->hash2 == h2 && elem->hash1 == h1 &&
        elem->hash_item.key_len == key_len &&
        memcmp(elem->hash_item.key, key, key_len) == 0) {
      *res = &elem->hash_item;
      co_return;
    }

    ++elem;
  }

  // orig code: elem = bin_at(hash, (h2 & mask));
  // read the data from HM the while loop below will access
  elem = static_cast<_cuckoo_hash_elem *>(co_await b.read_laddr(
      reinterpret_cast<uintptr_t>(bin_at(hash, (h2 & mask))),
      sizeof(struct _cuckoo_hash_elem) * hash->bin_size));
  end = elem + hash->bin_size;
  while (elem != end) {
    if (elem->hash2 == h1 && elem->hash1 == h2 &&
        elem->hash_item.key_len == key_len &&
        memcmp(elem->hash_item.key, key, key_len) == 0) {
      *res = &elem->hash_item;
      co_return;
    }

    ++elem;
  }

  *res = nullptr;
  co_return;
}

template <class T>
inline CoroRMC insert(Backend<T> &b, struct cuckoo_hash *hash,
                      struct _cuckoo_hash_elem *item, bool *success) {
  size_t max_depth = (size_t)hash->power << 5;
  if (max_depth > (size_t)hash->bin_size << hash->power)
    max_depth = (size_t)hash->bin_size << hash->power;

  uint32_t offset = 0;
  int phase = 0;
  while (phase < 2) {
    uint32_t mask = (1U << hash->power) - 1;

    for (size_t depth = 0; depth < max_depth; ++depth) {
      uint32_t h1m = item->hash1 & mask;

      // orig code: struct _cuckoo_hash_elem *beg = bin_at(hash, h1m);
      // read from HM the data the for loop below will iterate over
      struct _cuckoo_hash_elem *beg =
          static_cast<_cuckoo_hash_elem *>(co_await b.read_laddr(
              reinterpret_cast<uintptr_t>(bin_at(hash, h1m)),
              sizeof(struct _cuckoo_hash_elem) * hash->bin_size));
      struct _cuckoo_hash_elem *end = beg + hash->bin_size;

      for (struct _cuckoo_hash_elem *elem = beg; elem != end; ++elem) {
        if (elem->hash1 == elem->hash2 || (elem->hash1 & mask) != h1m) {
          *elem = *item;
          // write elem to HM
          co_await b.write_laddr(reinterpret_cast<uintptr_t>(elem), elem,
                                 sizeof(struct _cuckoo_hash_elem));
          *success = true;
          co_return;
        }
      }

      struct _cuckoo_hash_elem victim = beg[offset];

      beg[offset] = *item;
      // write beg[offset] to HM
      co_await b.write_laddr(reinterpret_cast<uintptr_t>(&beg[offset]),
                             &beg[offset], sizeof(struct _cuckoo_hash_elem));

      item->hash_item = victim.hash_item;
      item->hash1 = victim.hash2;
      item->hash2 = victim.hash1;

      if (++offset == hash->bin_size) offset = 0;
    }

    ++phase;

    if (phase == 1) {
      if (grow_table(hash))
        /* continue */;
      else
        break;
    }
  }

  if (grow_bin_size(hash)) {
    uint32_t mask = (1U << hash->power) - 1;
    struct _cuckoo_hash_elem *last = bin_at(hash, (item->hash1 & mask) + 1) - 1;

    *last = *item;
    *success = true;
  } else {
    die("undo_insert here\n");
    // return undo_insert(hash, item, max_depth, offset, phase);
  }
}

template <class T>
inline CoroRMC hash_insert(Backend<T> &b) {
  thread_local uint8_t key_id = 0;
  uint32_t h1, h2;
  void *value = reinterpret_cast<void *>(0xDEADBEEF);
  const void *key = &KEYS[key_id];

  if (++key_id == KEYS.size()) key_id = 0;

  compute_hash(key, KEY_LEN, &h1, &h2);

  struct cuckoo_hash_item *item;
  co_await rmclock.lock(b);
  co_await lookup(b, &b.table, key, KEY_LEN, h1, h2, &item);

  if (item) {
    // replace old value
    // item->value = value;
    value = reinterpret_cast<void *>(0xCAFEFEED);
    co_await b.write_laddr(reinterpret_cast<uintptr_t>(&item->value), &value,
                           sizeof(item->value));
    co_await rmclock.unlock(b);
    co_yield 1;
    co_return;
  }

  struct _cuckoo_hash_elem elem = {
      .hash_item = {.key = key, .key_len = KEY_LEN, .value = value},
      .hash1 = h1,
      .hash2 = h2};

  bool success;
  co_await insert(b, &b.table, &elem, &success);
  co_await rmclock.unlock(b);

  if (success) {
    b.table.count++;
    co_yield 1;
  } else {
    co_yield 0;
  }
}

template <class T>
inline CoroRMC hash_lookup(Backend<T> &b) {
  thread_local uint8_t key_id = 0;
  uint32_t h1, h2;
  const void *key = &KEYS[key_id];

  if (++key_id == KEYS.size()) key_id = 0;

  compute_hash(key, KEY_LEN, &h1, &h2);

  struct cuckoo_hash_item *res;
  co_await rmclock.lock(b);
  co_await lookup(b, &b.table, key, KEY_LEN, h1, h2, &res);
  co_await rmclock.unlock(b);

  if (res) {
    // std::cout << "value=" << std::hex << res->value << "\n";
    co_yield 1;
  } else {
    co_yield 0;
  }
}

#endif  // WORKLOAD_HASHTABLE

template <class T>
inline CoroRMC get_handler(RMCType type, Backend<T> &b) {
  switch (type) {
    case TRAVERSE_LL:
      return std::move(traversell.handler(b));
    case LOCK_TRAVERSE_LL:
      return std::move(locktraversell.handler(b));
    case RANDOM_WRITES:
      return std::move(randomwrites.handler(b));
#if defined(WORKLOAD_HASHTABLE)
    /* TODO: fix this mess */
    case HASHTABLE:
      // thread_local uint8_t num_gets = 0;
      // if (++num_gets > 1) {
      //  num_gets = 0;
      //  return std::move(hash_insert(backend));
      //}

      // return std::move(hash_lookup(backend));
      thread_local uint16_t num_gets = 0;

      if (num_gets > 20) return std::move(hash_lookup(b));

      num_gets++;
      if (num_gets > 20) printf("this is last insert\n");
      return std::move(hash_insert(b));
#endif
    default:
      die("no handler for rmctype: %d", type);
  }
}
