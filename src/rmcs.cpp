#include "rmcs.h"

#include <array>

class RMCTraverseLL : public RMCBase {
  struct LLNode {
    void *next;
    uint64_t data;
  };

  static constexpr size_t BUFSIZE = 1 << 29;  // 512 MB
  static constexpr uint32_t TOTAL_NODES = BUFSIZE / sizeof(LLNode);

  bool runtime_inited = false;
  uintptr_t rbaseaddr = 0;
  uint32_t length = 0;
  uint32_t rkey = 0;

  LLNode *server_linkdlst = nullptr;
  uint32_t start_node = 0;

  uintptr_t get_next_node_addr(uint32_t num_skip) {
    uint32_t next_node = start_node;
    start_node += num_skip;
    return rbaseaddr + next_node * sizeof(LLNode);
  }

 public:
  RMCTraverseLL() = default;

  ~RMCTraverseLL() {
    if (server_linkdlst) destroy_linkedlist(server_linkdlst);
  }
  RMCTraverseLL(const RMCTraverseLL &) = delete;
  RMCTraverseLL(RMCTraverseLL &&) = delete;
  RMCTraverseLL &operator=(const RMCTraverseLL &) = delete;
  RMCTraverseLL &operator=(RMCTraverseLL &&) = delete;

  CoroRMC runtime_handler(const BackendBase *b) final {
    int num_nodes = co_await b->get_param();
    uintptr_t addr = get_next_node_addr(num_nodes);

    LLNode *node = nullptr;
    int reply = 1;

    for (int i = 0; i < num_nodes; ++i) {
      node =
          static_cast<LLNode *>(co_await b->read(addr, sizeof(LLNode), rkey));
      addr = reinterpret_cast<uintptr_t>(node->next);
    }

    co_return &reply;
  }

  CoroRMC runtime_init(const ibv_mr &remote_mr) final {
    assert(!runtime_inited);
    runtime_inited = true;

    /* cache remote memory access information */
    rbaseaddr = reinterpret_cast<uintptr_t>(remote_mr.addr);
    length = remote_mr.length & 0xFFFFffff;
    rkey = remote_mr.rkey;

    printf("runtime_init() rbaseaddr=0x%lx\n", rbaseaddr);
    InitReply reply{rbaseaddr, length, rkey};
    co_return &reply;
  }

  ServerAlloc server_init(ServerAllocator &sa) final {
    ServerAlloc alloc = sa.request_memory(BUFSIZE);
    server_linkdlst = create_linkedlist<LLNode>(alloc.addr, BUFSIZE);
    return alloc;
  }

  static constexpr RMCType get_type() { return TRAVERSE_LL; }
};

class RMCLockTraverseLL : public RMCBase {
  struct LLNode {
    void *next;
    uint64_t data;
  };

  static constexpr size_t BUFSIZE = 1 << 29;  // 512 MB
  uintptr_t rbaseaddr = 0;
  uint32_t length = 0;
  uint32_t rkey = 0;
  LLNode *server_linkdlst = nullptr;
  uint32_t start_node = 0;
  RMCLock rmclock;

  uintptr_t get_next_node_addr(uint32_t num_skip) {
    uint32_t next_node = start_node;
    start_node += num_skip;
    return rbaseaddr + next_node * sizeof(LLNode);
  }

 public:
  RMCLockTraverseLL() = default;
  ~RMCLockTraverseLL() {
    if (server_linkdlst) destroy_linkedlist(server_linkdlst);
  }
  RMCLockTraverseLL(const RMCLockTraverseLL &) = delete;
  RMCLockTraverseLL(RMCLockTraverseLL &&) = delete;
  RMCLockTraverseLL &operator=(const RMCLockTraverseLL &) = delete;
  RMCLockTraverseLL &operator=(RMCLockTraverseLL &&) = delete;

  CoroRMC runtime_handler(const BackendBase *b) final {
    int num_nodes = co_await b->get_param();
    uintptr_t addr = get_next_node_addr(num_nodes);
    LLNode *node = nullptr;
    int reply = 1;

    for (int i = 0; i < num_nodes; ++i) {
      co_await rmclock.lock(b);
      node =
          static_cast<LLNode *>(co_await b->read(addr, sizeof(LLNode), rkey));
      addr = reinterpret_cast<uintptr_t>(node->next);
      co_await rmclock.unlock(b);
    }

    co_return &reply;
  }

  CoroRMC runtime_init(const ibv_mr &remote_mr) final {
    rbaseaddr = reinterpret_cast<uintptr_t>(remote_mr.addr);
    length = remote_mr.length & 0xFFFFffff;
    rkey = remote_mr.rkey;

    printf("runtime_init() rbaseaddr=0x%lx\n", rbaseaddr);
    InitReply reply{rbaseaddr, length, rkey};
    co_return &reply;
  }

  ServerAlloc server_init(ServerAllocator &sa) final {
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
  CoroRMC runtime_handler(const BackendBase *b) final {
    rt_assert(inited, "write RMC not inited");  // TODO: remove
    const uint32_t num_writes = co_await b->get_param();
    int reply = 1;

    for (auto i = 0u; i < num_writes; ++i) {
      // TODO: rkey
      co_await b->write(random_addr, &WRITE_VAL, sizeof(WRITE_VAL), 0);
      random_addr += 248;
    }

    co_return &reply;
  }

  CoroRMC runtime_init(const ibv_mr &mr) final {
    int reply = 1;
    co_return &reply;
  }

  ServerAlloc server_init(ServerAllocator &sa) final {
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

static RMCTraverseLL traversell;
static RMCLockTraverseLL locktraversell;
static RMCRandomWrites randomwrites;

/* data path */
static constexpr std::array<std::pair<RMCType, RMCBase *>, NUM_REG_RMC>
    rmc_values{{std::make_pair(RMCTraverseLL::get_type(), &traversell)}};

/* data path */
static constexpr auto rmc_map =
    StaticMap<RMCType, RMCBase *, rmc_values.size()>{{rmc_values}};

CoroRMC rmcs_get_init(RMCType type, const ibv_mr &mr) {
  return rmc_map.at(type)->runtime_init(mr);
}

/* data path */
CoroRMC rmcs_get_handler(const RMCType type, const BackendBase *b) {
  return rmc_map.at(type)->runtime_handler(b);
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

/* control path */
void rmcs_server_init(ServerAllocator &sa, std::vector<ServerAlloc> &allocs) {
  for (auto &rmc_pair : rmc_values)
    allocs.push_back(rmc_pair.second->server_init(sa));
}
