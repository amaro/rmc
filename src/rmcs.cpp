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
      cur->next = reinterpret_cast<RemoteAddr>(indices[i + 1]);
    else
      cur->next = reinterpret_cast<RemoteAddr>(indices[0]);

    cur->data = 1;
  }

  return linkedlist;
}

class RMCTraverseLL : public RMCBase {
  struct LLNode {
    RemoteAddr next;
    uint64_t data;
  };

  static constexpr size_t BUFSIZE = 1 << 29;  // 512 MB
  static constexpr uint32_t TOTAL_NODES = BUFSIZE / sizeof(LLNode);

  RemoteAddr rbaseaddr = 0;
  uint32_t length = 0;
  uint32_t rkey = 0;

  LLNode *server_linkdlst = nullptr;
  uint32_t start_node = 0;

  RemoteAddr get_next_node_addr(uint32_t num_skip) {
    uint32_t next_node = start_node;
    start_node += num_skip;
    return rbaseaddr + next_node * sizeof(LLNode);
  }

 public:
  RMCTraverseLL() = default;

  CoroRMC runtime_handler(const BackendBase *b, const ExecReq *req) final {
    // int num_nodes = co_await b->get_param();
    int num_nodes = 2;
    RemoteAddr addr = get_next_node_addr(num_nodes);
    int reply = 1;
    RemotePtr<LLNode> ptr(b, addr, rkey);

    for (int i = 0; i < num_nodes; ++i) {
      co_await ptr.read();
      LLNode &node = ptr.get_ref();
      ptr.set_raddr(node.next);
    }

    co_return &reply;
  }

  CoroRMC runtime_init(const MemoryRegion &rmc_mr) final {
    /* cache remote memory access information */
    rbaseaddr = reinterpret_cast<RemoteAddr>(rmc_mr.addr);
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

class RMCLockTraverseLL : public RMCBase {
  struct LLNode {
    RemoteAddr next;
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

  CoroRMC runtime_handler(const BackendBase *b, const ExecReq *req) final {
    // int num_nodes = co_await b->get_param();
    int num_nodes = 2;
    uintptr_t addr = get_next_node_addr(num_nodes);
    LLNode node;
    int reply = 1;

    for (int i = 0; i < num_nodes; ++i) {
      co_await rmclock.lock(b);
      co_await b->read(addr, &node, sizeof(LLNode), rkey);
      addr = reinterpret_cast<uintptr_t>(node.next);
      co_await rmclock.unlock(b);
    }

    co_return &reply;
  }

  CoroRMC runtime_init(const MemoryRegion &rmc_mr) final {
    rbaseaddr = reinterpret_cast<uintptr_t>(rmc_mr.addr);
    length = rmc_mr.length & 0xFFFFffff;
    rkey = rmc_mr.rdma.rkey;

    printf("runtime_init() rbaseaddr=0x%lx\n", rbaseaddr);
    InitReply reply{rbaseaddr, length, rkey};
    co_return &reply;
  }

  MemoryRegion server_init(MrAllocator &sa) final {
    MemoryRegion alloc = sa.request_memory(BUFSIZE);
    server_linkdlst = create_linkedlist<LLNode>(alloc.addr, BUFSIZE);
    return alloc;
  }

  static constexpr RMCType get_type() { return RMCType::LOCK_TRAVERSE_LL; }
};

class RMCUpdateLL : public RMCBase {
  struct LLNode {
    RemoteAddr next;
    uint64_t data;
  };

  static constexpr size_t BUFSIZE = 1 << 29;  // 512 MB
  static constexpr uint32_t TOTAL_NODES = BUFSIZE / sizeof(LLNode);

  RemoteAddr rbaseaddr = 0;
  uint32_t length = 0;
  uint32_t rkey = 0;

  LLNode *server_linkdlst = nullptr;
  uint32_t start_node = 0;

  RemoteAddr get_next_node_addr(uint32_t num_skip) {
    uint32_t next_node = start_node;
    start_node += num_skip;
    return rbaseaddr + next_node * sizeof(LLNode);
  }

 public:
  RMCUpdateLL() = default;

  CoroRMC runtime_handler(const BackendBase *b, const ExecReq *req) final {
    // int num_nodes = co_await b->get_param();
    int num_nodes = 2;
    RemoteAddr addr = get_next_node_addr(num_nodes);
    int reply = 1;
    RemotePtr<LLNode> ptr(b, addr, rkey);

    for (int i = 0; i < num_nodes; ++i) {
      co_await ptr.read();
      LLNode &node = ptr.get_ref();
      node.data++;
      co_await ptr.write();
      ptr.set_raddr(node.next);
    }

    co_return &reply;
  }

  CoroRMC runtime_init(const MemoryRegion &rmc_mr) final {
    /* cache remote memory access information */
    rbaseaddr = reinterpret_cast<RemoteAddr>(rmc_mr.addr);
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

namespace KVStore {
class RMC : public RMCBase {
  static constexpr size_t BUFSIZE = 1 << 30;  // 1 GB
  static constexpr size_t MAX_RECORDS = BUFSIZE / sizeof(Record);

  RemoteAddr tableaddr = 0;
  uint32_t length = 0;
  uint32_t rkey = 0;
  uint32_t current_key = 0;

  Record *server_table = nullptr;

 public:
  RMC() = default;

  CoroRMC runtime_handler(const BackendBase *b, const ExecReq *req) final {
    auto *kvreq = reinterpret_cast<const RpcReq *>(req->data);

    RemotePtr<Record> rowptr(b, tableaddr, rkey);
    // size_t index = hash_buff(kvreq->record.key) % MAX_RECORDS;
    size_t index =
        *(reinterpret_cast<const uint32_t *>(kvreq->record.key)) % MAX_RECORDS;

    rowptr.set_raddr(rowptr.raddr_for_index(index));
    co_await rowptr.read();
    Record &record = rowptr.get_ref();

    if (kvreq->reqtype == RpcReqType::GET) {
      uint8_t reply_val[VAL_LEN];
      std::memcpy(&reply_val, &record.val, VAL_LEN);
      co_return &reply_val;
    }

    std::memcpy(&record, &kvreq->record, sizeof(Record));
    co_await rowptr.write();
    int reply = 1;
    co_return &reply;
  }

  CoroRMC runtime_init(const MemoryRegion &rmc_mr) final {
    /* cache remote memory access information */
    tableaddr = reinterpret_cast<RemoteAddr>(rmc_mr.addr);
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
    assert(reinterpret_cast<RemoteAddr>(alloc.addr) % 4096 == 0);

    server_table = new (alloc.addr) Record[MAX_RECORDS];
    for (auto i = 0u; i < MAX_RECORDS; i++)
      std::memset(&server_table[i].val, 1, VAL_LEN);

    printf("server_tableaddr=%p\n", static_cast<void *>(server_table));
    return alloc;
  }

  static constexpr RMCType get_type() { return RMCType::KVSTORE; }
};
}  // namespace KVStore

static RMCTraverseLL traversell;
static RMCLockTraverseLL locktraversell;
static RMCUpdateLL updatell;
static KVStore::RMC kvstore;

/* data path */
static constexpr std::array<std::pair<RMCType, RMCBase *>, NUM_REG_RMC>
    rmc_values{{std::make_pair(KVStore::RMC::get_type(), &kvstore)}};

/* data path */
static constexpr auto rmc_map =
    StaticMap<RMCType, RMCBase *, rmc_values.size()>{{rmc_values}};

/* control path */
CoroRMC rmcs_get_init(RMCType type, const MemoryRegion &mr) {
  return rmc_map.at(type)->runtime_init(mr);
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
