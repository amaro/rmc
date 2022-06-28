#pragma once

#include "allocator.h"
#include "backend.h"
#include "config.h"

enum RMCType : int { TRAVERSE_LL, LOCK_TRAVERSE_LL, RANDOM_WRITES, HASHTABLE };

struct RMCBase {
  virtual ~RMCBase() {}
  /* Runs at the runtime location, main handler for this RMC */
  virtual CoroRMC runtime_handler(const BackendBase *b) = 0;
  /* Runs at the runtime location. Receives memory region information such that
     we can cache it at the class level, or return if appropriate. */
  virtual CoroRMC runtime_init(const ibv_mr &mr) = 0;
  /* Runs at the server: requests memory, and gets access to it, so
     it can be initialized. */
  virtual ServerAlloc server_init(ServerAllocator &sa) = 0;
};

class RMCTraverseLL : public RMCBase {
  static constexpr size_t BUFSIZE = 1 << 29;  // 512 MB
  bool runtime_inited = false;
  uintptr_t rbaseaddr = 0;
  uint32_t length = 0;
  uint32_t rkey = 0;

  LLNode *server_linkdlst = nullptr;

 public:
  ~RMCTraverseLL() {
    if (server_linkdlst) destroy_linkedlist(server_linkdlst);
  }

  CoroRMC runtime_handler(const BackendBase *b) override {
    int num_nodes = co_await b->get_param();
    uintptr_t addr = b->get_baseaddr(num_nodes);
    LLNode *node = nullptr;
    int reply = 1;

    for (int i = 0; i < num_nodes; ++i) {
      node = static_cast<LLNode *>(co_await b->read(addr, sizeof(LLNode)));
      addr = reinterpret_cast<uintptr_t>(node->next);
    }

    co_return &reply;
  }

  CoroRMC runtime_init(const ibv_mr &remote_mr) override {
    assert(!runtime_inited);
    runtime_inited = true;

    puts("RMCTraverseLL runtime_init() called");
    /* cache remote memory access information */
    rbaseaddr = reinterpret_cast<uintptr_t>(remote_mr.addr);
    length = remote_mr.length & 0xFFFFffff;
    rkey = remote_mr.rkey;

    InitReply reply{rbaseaddr, length, rkey};
    co_return &reply;
  }

  ServerAlloc server_init(ServerAllocator &sa) override {
    ServerAlloc alloc = sa.request_memory(BUFSIZE);
    server_linkdlst = create_linkedlist<LLNode>(alloc.addr, BUFSIZE);
    return alloc;
  }

  static constexpr RMCType get_type() { return TRAVERSE_LL; }
};

class RMCLockTraverseLL : public RMCBase {
  static constexpr size_t BUFSIZE = 1 << 29;  // 512 MB
  bool inited = false;
  LLNode *server_linkdlst = nullptr;
  RMCLock rmclock;

 public:
  ~RMCLockTraverseLL() {
    if (server_linkdlst) destroy_linkedlist(server_linkdlst);
  }

  CoroRMC runtime_handler(const BackendBase *b) override {
    int num_nodes = co_await b->get_param();
    uintptr_t addr = b->get_baseaddr(num_nodes);
    LLNode *node = nullptr;
    int reply = 1;

    for (int i = 0; i < num_nodes; ++i) {
      co_await rmclock.lock(b);
      node = static_cast<LLNode *>(co_await b->read(addr, sizeof(LLNode)));
      addr = reinterpret_cast<uintptr_t>(node->next);
      co_await rmclock.unlock(b);
    }

    co_return &reply;
  }

  CoroRMC runtime_init(const ibv_mr &mr) override {
    int reply = 1;
    co_return &reply;
  }

  ServerAlloc server_init(ServerAllocator &sa) override {
    ServerAlloc alloc = sa.request_memory(BUFSIZE);
    server_linkdlst = create_linkedlist<LLNode>(alloc.addr, BUFSIZE);
    return alloc;
  }
  static constexpr RMCType get_type() { return LOCK_TRAVERSE_LL; }
};

class RMCRandomWrites : public RMCBase {
  static constexpr size_t BUFSIZE = 1 << 29;  // 512 MB
  static constexpr uint64_t WRITE_VAL = 0xDEADBEEF;
  bool inited = false;
  uintptr_t random_addr = 0;

 public:
  ~RMCRandomWrites() {}

  CoroRMC runtime_handler(const BackendBase *b) override {
    rt_assert(inited, "write RMC not inited");  // TODO: remove
    const uint32_t num_writes = co_await b->get_param();
    int reply = 1;

    for (auto i = 0u; i < num_writes; ++i) {
      co_await b->write_raddr(random_addr, &WRITE_VAL, sizeof(WRITE_VAL));
      random_addr += 248;
    }

    co_return &reply;
  }

  CoroRMC runtime_init(const ibv_mr &mr) override {
    int reply = 1;
    co_return &reply;
  }
  ServerAlloc server_init(ServerAllocator &sa) override {
    return sa.request_memory(BUFSIZE);
  }
  static constexpr RMCType get_type() { return RANDOM_WRITES; }
};

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

inline static RMCTraverseLL traversell;
inline static RMCLockTraverseLL locktraversell;
inline static RMCRandomWrites randomwrites;

/* data path */
// static constexpr std::array<std::pair<RMCType, RMCBase *>, NUM_REG_RMC>
// rmc_values{
//    {std::make_pair(TRAVERSE_LL, &traversell),
//     std::make_pair(LOCK_TRAVERSE_LL, &locktraversell),
//     std::make_pair(RANDOM_WRITES, &randomwrites)}};
inline static constexpr std::array<std::pair<RMCType, RMCBase *>, NUM_REG_RMC>
    rmc_values{{std::make_pair(RMCTraverseLL::get_type(), &traversell)}};

/* data path */
inline static constexpr auto rmc_map =
    StaticMap<RMCType, RMCBase *, rmc_values.size()>{{rmc_values}};

/* control path */
inline CoroRMC rmcs_get_init(RMCType type, const ibv_mr &mr) {
  return std::move(rmc_map.at(type)->runtime_init(mr));
}

/* control path */
inline void rmcs_server_init(ServerAllocator &sa,
                             std::vector<ServerAlloc> &allocs) {
  for (auto &rmc_pair : rmc_values)
    allocs.push_back(rmc_pair.second->server_init(sa));
}

/* data path */
inline CoroRMC rmcs_get_handler(const RMCType type, const BackendBase *b) {
  return std::move(rmc_map.at(type)->runtime_handler(b));
  //#if defined(WORKLOAD_HASHTABLE)
  //  /* TODO: fix this mess */
  //  case HASHTABLE:
  //    // thread_local uint8_t num_gets = 0;
  //    // if (++num_gets > 1) {
  //    //  num_gets = 0;
  //    //  return std::move(hash_insert(backend));
  //    //}
  //
  //    // return std::move(hash_lookup(backend));
  //    thread_local uint16_t num_gets = 0;
  //
  //    if (num_gets > 20) return std::move(hash_lookup(b));
  //
  //    num_gets++;
  //    if (num_gets > 20) printf("this is last insert\n");
  //    return std::move(hash_insert(b));
  //#endif
}
