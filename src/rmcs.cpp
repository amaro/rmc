#include "rmcs.h"

#include <array>
#include <functional>

/* creates a randomized linked list over an already allocated *buffer */
template <typename T>
inline T *create_linkedlist(void *buffer, size_t bufsize) {
  size_t num_nodes = bufsize / sizeof(T);
  std::vector<T *> indices(num_nodes);
  T *linkedlist = new (buffer) T[num_nodes];

  for (auto i = 0u; i < num_nodes; ++i) indices[i] = &linkedlist[i];

  printf("Shuffling %lu linked list nodes at addr %p\n", num_nodes, buffer);
  shuffle_vec(indices, RANDOM_SEED);
  // auto rng = std::default_random_engine{RANDOM_SEED};
  // std::shuffle(std::begin(indices) + 1, std::end(indices), rng);

  for (auto i = 0u; i < num_nodes; ++i) {
    T *cur = indices[i];
    if (i < num_nodes - 1)
      cur->next = reinterpret_cast<AppAddr>(indices[i + 1]);
    else
      cur->next = reinterpret_cast<AppAddr>(indices[0]);

    cur->data = 1;
  }

  return linkedlist;
}

namespace TraverseLL {
class Simple : public RMCBase {
  struct LLNode {
    AppAddr next;
    uint64_t data;
  };

  static constexpr size_t BUFSIZE = 1 << 29;  // 512 MB
  static constexpr uint32_t TOTAL_NODES = BUFSIZE / sizeof(LLNode);

  AppAddr rbaseaddr = 0;
  uint32_t length = 0;
  uint32_t rkey = 0;

  LLNode *server_linkdlst = nullptr;
  uint32_t start_node = 0;

  AppAddr get_next_node_addr(uint32_t num_skip) {
    uint32_t next_node = start_node;
    start_node += num_skip;
    return rbaseaddr + next_node * sizeof(LLNode);
  }

 public:
  Simple() = default;

  CoroRMC runtime_handler(const BackendBase *b, const ExecReq *req) final {
    auto *args = reinterpret_cast<const RpcReq *>(req->data);
    int num_nodes = args->num_nodes;
    int reply = 1;
    AppPtr<LLNode> ptr(b, get_next_node_addr(num_nodes), rkey);

    for (int i = 0; i < num_nodes; ++i) {
      co_await ptr.read();
      LLNode &node = ptr.get_ref();
      ptr.set_raddr(node.next);
    }

    co_return &reply;
  }

  CoroRMC runtime_init(const MemoryRegion &rmc_mr,
                       std::deque<std::coroutine_handle<>> *runqueue) final {
    /* cache remote memory access information */
    rbaseaddr = reinterpret_cast<AppAddr>(rmc_mr.addr);
    length = rmc_mr.length & 0xFFFFffff;
    rkey = rmc_mr.rdma.rkey;

    printf("RMCTraverseLL runtime_init() rbaseaddr=0x%lx\n", rbaseaddr);
    InitReply reply{rbaseaddr, length, rkey};
    co_return &reply;
  }

  MemoryRegion server_init(MrAllocator &sa) final {
    puts("RMCTraverseLL server_init()");
    MemoryRegion alloc = sa.request_memory(BUFSIZE);
    server_linkdlst = create_linkedlist<LLNode>(alloc.addr, BUFSIZE);
    return alloc;
  }

  static constexpr RMCType get_type() { return RMCType::TRAVERSE_LL; }
};

class Multi : public RMCBase {
  struct LLNode {
    AppAddr next;
    uint64_t data;
  };

  static constexpr size_t BUFSIZE = 1 << 29;  // 512 MB
  static constexpr uint32_t TOTAL_NODES = BUFSIZE / sizeof(LLNode);

  AppAddr rbaseaddr = 0;
  uint32_t length = 0;
  uint32_t rkey = 0;

  LLNode *server_linkdlst = nullptr;
  uint32_t start_node = 0;

  AppAddr get_next_node_addr(uint32_t num_skip) {
    uint32_t next_node = start_node;
    start_node += num_skip;
    return rbaseaddr + next_node * sizeof(LLNode);
  }

 public:
  Multi() = default;

  CoroRMC runtime_handler(const BackendBase *b, const ExecReq *req) final {
    auto *args = reinterpret_cast<const RpcReq *>(req->data);
    int num_nodes = args->num_nodes;
    int reply = 1;
    AppAddr addr = get_next_node_addr(num_nodes);
    std::array<AppPtr<LLNode>, 4> ptrs = {
        AppPtr<LLNode>(b, addr, rkey),
        AppPtr<LLNode>(b, addr + sizeof(LLNode), rkey),
        AppPtr<LLNode>(b, addr + sizeof(LLNode) * 2, rkey),
        AppPtr<LLNode>(b, addr + sizeof(LLNode) * 3, rkey)};

    for (int i = 0; i < num_nodes; ++i) {
      co_await multiread<4>(b, ptrs);
      LLNode &node1 = ptrs[0].get_ref();
      LLNode &node2 = ptrs[1].get_ref();
      LLNode &node3 = ptrs[2].get_ref();
      LLNode &node4 = ptrs[3].get_ref();
      ptrs[0].set_raddr(node1.next);
      ptrs[1].set_raddr(node2.next);
      ptrs[2].set_raddr(node3.next);
      ptrs[3].set_raddr(node4.next);
    }

    co_return &reply;
  }

  CoroRMC runtime_init(const MemoryRegion &rmc_mr,
                       std::deque<std::coroutine_handle<>> *runqueue) final {
    /* cache remote memory access information */
    rbaseaddr = reinterpret_cast<AppAddr>(rmc_mr.addr);
    length = rmc_mr.length & 0xFFFFffff;
    rkey = rmc_mr.rdma.rkey;

    printf("RMCTraverseLL runtime_init() rbaseaddr=0x%lx\n", rbaseaddr);
    InitReply reply{rbaseaddr, length, rkey};
    co_return &reply;
  }

  MemoryRegion server_init(MrAllocator &sa) final {
    puts("RMCTraverseLL server_init()");
    MemoryRegion alloc = sa.request_memory(BUFSIZE);
    server_linkdlst = create_linkedlist<LLNode>(alloc.addr, BUFSIZE);
    return alloc;
  }

  static constexpr RMCType get_type() { return RMCType::MULTI_TRAVERSE_LL; }
};

class Locked : public RMCBase {
  struct LLNode {
    AppAddr next;
    uint64_t data;
  };

  static constexpr size_t LOCKSZ = 64;
  static constexpr size_t BUFSZ = (1 << 29) - LOCKSZ;  // 512 MB - 64
  AppAddr rlock = 0;
  AppAddr rlinkedlist = 0;
  uint32_t length = 0;
  uint32_t rkey = 0;
  uint32_t start_node = 0;
  RMCLock wiclock;

  AppAddr get_next_node_addr(uint32_t num_skip) {
    uint32_t next_node = start_node;
    start_node += num_skip;
    return rlinkedlist + next_node * sizeof(LLNode);
  }

 public:
  Locked() = default;

  CoroRMC runtime_handler(const BackendBase *b, const ExecReq *req) final {
    auto *args = reinterpret_cast<const RpcReq *>(req->data);
    int num_nodes = args->num_nodes;
    int reply = 1;
    uint64_t llock;
    AppPtr<LLNode> ptr(b, get_next_node_addr(num_nodes), rkey);

    for (int i = 0; i < num_nodes; ++i) {
      co_await wiclock.lock(b, llock);
      co_await ptr.read();
      LLNode &node = ptr.get_ref();
      co_await wiclock.unlock(b, llock);
      ptr.set_raddr(node.next);
    }

    co_return &reply;
  }

  CoroRMC runtime_init(const MemoryRegion &rmc_mr,
                       std::deque<std::coroutine_handle<>> *runqueue) final {
    /* first 64 bytes of alloc.addr are reserved for RMCLock,
       remaining bytes are used to store linked list */
    rlock = reinterpret_cast<AppAddr>(rmc_mr.addr);
    rlinkedlist = rlock + LOCKSZ;
    length = rmc_mr.length & 0xFFFFffff;
    rkey = rmc_mr.rdma.rkey;

    wiclock.init_runtime(rlock, rkey, runqueue);

    InitReply reply{rlock, length, rkey};
    co_return &reply;
  }

  MemoryRegion server_init(MrAllocator &sa) final {
    /* first 64 bytes of alloc.addr are reserved for RMCLock,
       remaining bytes are used to store linked list */
    MemoryRegion alloc = sa.request_memory(BUFSZ);

    wiclock.init_server(alloc.addr);
    create_linkedlist<LLNode>(static_cast<uint8_t *>(alloc.addr) + LOCKSZ,
                              BUFSZ);
    return alloc;
  }

  static constexpr RMCType get_type() { return RMCType::LOCK_TRAVERSE_LL; }
};

class Update : public RMCBase {
  struct LLNode {
    AppAddr next;
    uint64_t data;
  };

  static constexpr size_t BUFSIZE = 1 << 29;  // 512 MB
  static constexpr uint32_t TOTAL_NODES = BUFSIZE / sizeof(LLNode);

  AppAddr rbaseaddr = 0;
  uint32_t length = 0;
  uint32_t rkey = 0;

  LLNode *server_linkdlst = nullptr;
  uint32_t start_node = 0;

  AppAddr get_next_node_addr(uint32_t num_skip) {
    uint32_t next_node = start_node;
    start_node += num_skip;
    return rbaseaddr + next_node * sizeof(LLNode);
  }

 public:
  Update() = default;

  CoroRMC runtime_handler(const BackendBase *b, const ExecReq *req) final {
    auto *args = reinterpret_cast<const RpcReq *>(req->data);
    int num_nodes = args->num_nodes;
    AppAddr addr = get_next_node_addr(num_nodes);
    int reply = 1;
    AppPtr<LLNode> ptr(b, addr, rkey);

    for (int i = 0; i < num_nodes; ++i) {
      co_await ptr.read();
      LLNode &node = ptr.get_ref();
      node.data++;
      co_await ptr.write();
      ptr.set_raddr(node.next);
    }

    co_return &reply;
  }

  CoroRMC runtime_init(const MemoryRegion &rmc_mr,
                       std::deque<std::coroutine_handle<>> *runqueue) final {
    /* cache remote memory access information */
    rbaseaddr = reinterpret_cast<AppAddr>(rmc_mr.addr);
    length = rmc_mr.length & 0xFFFFffff;
    rkey = rmc_mr.rdma.rkey;

    printf("RMCUpdateLL runtime_init() rbaseaddr=0x%lx\n", rbaseaddr);
    InitReply reply{rbaseaddr, length, rkey};
    co_return &reply;
  }

  MemoryRegion server_init(MrAllocator &sa) final {
    puts("RMCUpdateLL server_init()");
    MemoryRegion alloc = sa.request_memory(BUFSIZE);
    server_linkdlst = create_linkedlist<LLNode>(alloc.addr, BUFSIZE);
    return alloc;
  }

  static constexpr RMCType get_type() { return RMCType::UPDATE_LL; }
};
}  // namespace TraverseLL

namespace KVStore {
class RMC : public RMCBase {
  static constexpr size_t BUFSIZE = 1 << 30;  // 1 GB
  static constexpr size_t MAX_RECORDS = BUFSIZE / sizeof(Record);

  AppAddr tableaddr = 0;
  uint32_t length = 0;
  uint32_t rkey = 0;
  uint32_t current_key = 0;

  Record *server_table = nullptr;

 public:
  RMC() = default;

  CoroRMC runtime_handler(const BackendBase *b, const ExecReq *req) final {
    uint8_t get_reply[VAL_LEN];
    int put_reply;
    auto *kvreq = reinterpret_cast<const RpcReq *>(req->data);

    AppPtr<Record> rowptr(b, tableaddr, rkey);
    // size_t index = hash_buff(kvreq->record.key) % MAX_RECORDS;
    size_t index =
        *(reinterpret_cast<const uint32_t *>(kvreq->record.key)) % MAX_RECORDS;

    rowptr.set_raddr(rowptr.raddr_for_index(index));
    /* get a reference to this record (invalid at this point) */
    Record &record = rowptr.get_ref();

    if (kvreq->reqtype == RpcReqType::GET) {
      /* read record from server memory; makes &record valid */
      co_await rowptr.read();
      /* copy read record into reply buffer */
      std::memcpy(&get_reply, &record.val, VAL_LEN);
      /* TODO: can we just co_return &record.val ? */
      co_return &get_reply;
    } else {
      /* copy record from request arg to &record; makes &record valid */
      std::memcpy(&record, &kvreq->record, sizeof(Record));
      /* write &record to server memory */
      co_await rowptr.write();
      put_reply = 1;
      co_return &put_reply;
    }
  }

  CoroRMC runtime_init(const MemoryRegion &rmc_mr,
                       std::deque<std::coroutine_handle<>> *runqueue) final {
    /* cache remote memory access information */
    tableaddr = reinterpret_cast<AppAddr>(rmc_mr.addr);
    length = rmc_mr.length & 0xFFFFffff;
    rkey = rmc_mr.rdma.rkey;

    printf("RMC kvstore runtime_init() tableaddr=0x%lx\n", tableaddr);
    InitReply reply{tableaddr, length, rkey};
    co_return &reply;
  }

  MemoryRegion server_init(MrAllocator &sa) final {
    puts("RMC kvstore server_init()");
    MemoryRegion alloc = sa.request_memory(BUFSIZE);
    assert(alloc.length == BUFSIZE);
    assert(reinterpret_cast<AppAddr>(alloc.addr) % 4096 == 0);

    server_table = new (alloc.addr) Record[MAX_RECORDS];
    for (auto i = 0u; i < MAX_RECORDS; i++)
      std::memset(&server_table[i].val, 1, VAL_LEN);

    printf("server_tableaddr=%p\n", static_cast<void *>(server_table));
    return alloc;
  }

  static constexpr RMCType get_type() { return RMCType::KVSTORE; }
};
}  // namespace KVStore

namespace TAO {
class RMC : public RMCBase {
  static constexpr size_t BUFSIZE = 1 << 30;  // 1 GB; TODO: use at least 2GB
  static constexpr size_t ASSOC_LIST_LEN = 4;

  struct ObjectRow {
    uint8_t key[OBJ_KEY_LEN];
    uint8_t val[OBJ_VAL_LEN];
  };

  struct AssocRow {
    enum class Type { UNUSED, LIST, VALUE } type;
    union {
      uint8_t list[ASSOC_KEY_LEN * ASSOC_LIST_LEN];
      uint8_t val[ASSOC_VAL_LEN];
    } value;
  };

  static constexpr size_t OBJTABLE_SZ = BUFSIZE / 2;
  static constexpr size_t OBJTABLE_MAX_ROWS = OBJTABLE_SZ / sizeof(ObjectRow);
  static constexpr size_t ASSOCTABLE_SZ = BUFSIZE / 2;
  static constexpr size_t ASSOCTABLE_MAX_ROWS =
      ASSOCTABLE_SZ / sizeof(AssocRow);
  static constexpr size_t ASSOCTABLE_MAX_LISTS = 1000000;
  /* max lists must be less than max rows / 5, since each list will point to 4
   * assoc values, and these live in the same table. max lists will also affect
   * collision probability. */
  static_assert(ASSOCTABLE_MAX_LISTS < ASSOCTABLE_MAX_ROWS / 5);

  AppAddr objtable_addr = 0;
  AppAddr assoctable_addr = 0;
  uint32_t length = 0;
  uint32_t rkey = 0;
  uint32_t current_key = 0;

  ObjectRow *srv_objtable = nullptr;
  AssocRow *srv_assoctable = nullptr;

 public:
  RMC() = default;

  CoroRMC runtime_handler(const BackendBase *b, const ExecReq *req) final {
    auto *taoreq = reinterpret_cast<const RpcReq *>(req->data);
    uint32_t index;

    if (taoreq->reqtype == RpcReqType::GET) {
      AppPtr<ObjectRow> getop(b, objtable_addr, rkey);

      /* we don't hash indices to objtable */
      index = *(reinterpret_cast<const uint32_t *>(taoreq->params.get_key)) %
              OBJTABLE_MAX_ROWS;
      getop.set_raddr(getop.raddr_for_index(index));
      co_await getop.read(); /* suspend */
      ObjectRow &obj = getop.get_ref();

      /* reply val buffer */
      co_return &obj.val;
    } else {
      assert(taoreq->reqtype == RpcReqType::GET_ASSOC);
      /* TODO: for how long is this safe? */
      auto *param_key = &taoreq->params.get_assoc_key;
      uint8_t assoc_key[ASSOC_KEY_LEN] = {0};
      std::memcpy(&assoc_key, param_key, OBJ_KEY_LEN + ASSOC_TYPE_LEN);
      AppPtr<AssocRow> getop(b, assoctable_addr, rkey);

      /* prepare to issue the simple get(). assoc_key is 18 bytes but will only
       * use 10 (id1 + assoc type) for now. */
      index = *(reinterpret_cast<uint32_t *>(assoc_key));
      getop.set_raddr(getop.raddr_for_index(index));

      /* issue the "get" */
      co_await getop.read(); /* suspend */
      AssocRow &assoc = getop.get_ref();
      assert(assoc.type == AssocRow::Type::LIST);
      uint8_t *assoc_list = assoc.value.list;

      /* build 4 new AppPtrs */
      std::array<AppPtr<AssocRow>, ASSOC_LIST_LEN> multigetop = {
          AppPtr<AssocRow>(b, assoctable_addr, rkey),
          AppPtr<AssocRow>(b, assoctable_addr, rkey),
          AppPtr<AssocRow>(b, assoctable_addr, rkey),
          AppPtr<AssocRow>(b, assoctable_addr, rkey)};

      /* iterate over the list and set the 4 AppPtrs' raddrs. modify assoc_key
       * but only 8 last bytes */
      for (auto i = 0u; i < ASSOC_LIST_LEN; i++) {
        uint8_t *id2 =
            &assoc_list[(i * ASSOC_KEY_LEN) + OBJ_KEY_LEN + ASSOC_TYPE_LEN];
        /* assoc_key[10..14] = id2 */
        index = *(reinterpret_cast<uint32_t *>(id2));
        /* set the AppPtr raddr */
        multigetop[i].set_raddr(multigetop[i].raddr_for_index(index));
      }

      co_await multiread<ASSOC_LIST_LEN>(b, multigetop); /* suspends */
      uint8_t reply[ASSOC_VAL_LEN * ASSOC_LIST_LEN];

      /* iterate over AppPtrs and copy the resulting values to reply */
      for (auto i = 0u; i < ASSOC_LIST_LEN; i++) {
        AssocRow &a = multigetop[i].get_ref();
        assert(a.type == AssocRow::Type::VALUE);
        memcpy(&reply[i * ASSOC_VAL_LEN], &a.value.val, ASSOC_VAL_LEN);
      }

      co_return &reply;
    }
  }

  CoroRMC runtime_init(const MemoryRegion &rmc_mr,
                       std::deque<std::coroutine_handle<>> *runqueue) final {
    /* cache remote memory access information */
    objtable_addr = reinterpret_cast<AppAddr>(rmc_mr.addr);
    assoctable_addr = reinterpret_cast<AppAddr>(rmc_mr.addr) + OBJTABLE_SZ;
    length = rmc_mr.length & 0xFFFFffff;
    rkey = rmc_mr.rdma.rkey;

    printf("RMC TAO runtime_init() objtable_addr=0x%lx assoctable_addr=0x%lx\n",
           objtable_addr, assoctable_addr);
    InitReply reply{objtable_addr, length, rkey};
    co_return &reply;
  }

  MemoryRegion server_init(MrAllocator &sa) final {
    puts("RMC TAO server_init()");
    MemoryRegion alloc = sa.request_memory(BUFSIZE);
    assert(alloc.length == BUFSIZE);

    srv_objtable = new (alloc.addr) ObjectRow[OBJTABLE_MAX_ROWS];
    for (auto i = 0u; i < OBJTABLE_MAX_ROWS; i++)
      std::memset(&srv_objtable[i].val, 5, OBJ_VAL_LEN);
    printf("srv_objtable=%p\n", static_cast<void *>(srv_objtable));

    /* this is the maximum capacity we have, but we will only use MAX_LISTS */
    void *assoctable = reinterpret_cast<void *>(
        reinterpret_cast<uintptr_t>(alloc.addr) + OBJTABLE_SZ);
    srv_assoctable = new (assoctable) AssocRow[ASSOCTABLE_MAX_ROWS];
    for (auto i = 0u; i < ASSOCTABLE_MAX_ROWS; i++)
      srv_assoctable[i].type = AssocRow::Type::UNUSED;

    uint8_t assoc_key[ASSOC_KEY_LEN] = {0}; /* 18 bytes */
    for (uint32_t listi = 0u; listi < ASSOCTABLE_MAX_LISTS; listi++) {
      memcpy(assoc_key, &listi, 4);
      size_t index = listi;

      /* get the assoc row that corresponds to index, set it as LIST, and
       * initialize list to 0 */
      AssocRow &assoc_row = srv_assoctable[index];
      assert(assoc_row.type == AssocRow::Type::UNUSED);
      assoc_row.type = AssocRow::Type::LIST;
      uint8_t *assoc_list = assoc_row.value.list;
      bool print = false;

      memset(assoc_list, 0, ASSOC_KEY_LEN * ASSOC_LIST_LEN);

      /* for each association in this list, build an association value and add
       * it to the table */
      for (auto associ = 0u; associ < ASSOC_LIST_LEN; associ++) {
        /* try to prevent overwriting lists with values, use index */
        index = ASSOCTABLE_MAX_LISTS * (associ + 1) + listi;

        AssocRow &assoc_row2 = srv_assoctable[index];
        assert(assoc_row2.type == AssocRow::Type::UNUSED);
        assoc_row2.type = AssocRow::Type::VALUE;
        /* set associ's val as the loop index*/
        memset(assoc_row2.value.val, associ, ASSOC_VAL_LEN);

        /* Finally, append id2 at the end of this assoc list's assoc key. Note:
         * This is different from what fill_tao() in splinter does, for some
         * reason they seem to assume the list is only 64 bytes long, but it
         * should be ASSOC_VAL_LEN * 4 = 72. They also seem to be writing to
         * the beginning of the assoc, rather than to the end. But their
         * extension code seems to be reading from the end. */
        memcpy(
            &assoc_list[associ * ASSOC_KEY_LEN + OBJ_KEY_LEN + ASSOC_TYPE_LEN],
            &index, 4);
      }

      if (print) {
        print_buffer(assoc_list, ASSOC_KEY_LEN * ASSOC_LIST_LEN);
      }
    }

    printf("srv_assoctable=%p\n", static_cast<void *>(srv_assoctable));
    return alloc;
  }

  static constexpr RMCType get_type() { return RMCType::TAO; }
};
}  // namespace TAO

static TraverseLL::Simple traversell;
static TraverseLL::Multi multitraversell;
static TraverseLL::Locked locktraversell;
static TraverseLL::Update updatell;
static KVStore::RMC kvstore;
static TAO::RMC tao;

/* data path */
static constexpr std::array<std::pair<RMCType, RMCBase *>, NUM_REG_RMC>
    rmc_values{{std::make_pair(TAO::RMC::get_type(), &tao)}};

/* data path */
static constexpr auto rmc_map =
    StaticMap<RMCType, RMCBase *, rmc_values.size()>{{rmc_values}};

/* control path */
CoroRMC rmcs_get_init(RMCType type, const MemoryRegion &mr,
                      std::deque<std::coroutine_handle<>> *runqueue) {
  return rmc_map.at(type)->runtime_init(mr, runqueue);
}

/* data path */
CoroRMC rmcs_get_handler(const RMCType type, const BackendBase *b,
                         const ExecReq *req) {
  return rmc_map.at(type)->runtime_handler(b, req);
}

/* control path
   TODO: change to std::array<MemoryRegion, NUM_REG_RMC> & */
void rmcs_server_init(MrAllocator &sa, std::vector<MemoryRegion> &allocs) {
  for (auto &rmc_pair : rmc_values)
    allocs.push_back(rmc_pair.second->server_init(sa));
}
