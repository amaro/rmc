#include "rmcs.h"

#include <array>

class RMCTraverseLL : public RMCBase {
  struct LLNode {
    LLNode *next;
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

  ~RMCTraverseLL() {
    if (server_linkdlst) destroy_linkedlist(server_linkdlst);
  }
  RMCTraverseLL(const RMCTraverseLL &) = delete;
  RMCTraverseLL(RMCTraverseLL &&) = delete;
  RMCTraverseLL &operator=(const RMCTraverseLL &) = delete;
  RMCTraverseLL &operator=(RMCTraverseLL &&) = delete;

  CoroRMC runtime_handler(const BackendBase *b) final {
    int num_nodes = co_await b->get_param();
    RemoteAddr addr = get_next_node_addr(num_nodes);
    int reply = 1;
    RemotePtr<LLNode> ptr(addr);

    for (int i = 0; i < num_nodes; ++i) {
      co_await read(b, ptr, rkey);
      LLNode &node = ptr.get();
      ptr.setptr(node.next);
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
    LLNode *next;
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

  ~RMCUpdateLL() {
    if (server_linkdlst) destroy_linkedlist(server_linkdlst);
  }
  RMCUpdateLL(const RMCUpdateLL &) = delete;
  RMCUpdateLL(RMCUpdateLL &&) = delete;
  RMCUpdateLL &operator=(const RMCUpdateLL &) = delete;
  RMCUpdateLL &operator=(RMCUpdateLL &&) = delete;

  CoroRMC runtime_handler(const BackendBase *b) final {
    int num_nodes = co_await b->get_param();
    RemoteAddr addr = get_next_node_addr(num_nodes);
    int reply = 1;
    RemotePtr<LLNode> ptr(addr);

    for (int i = 0; i < num_nodes; ++i) {
      co_await read(b, ptr, rkey);
      LLNode &node = ptr.get();
      node.data++;
      co_await write(b, ptr, rkey);
      ptr.setptr(node.next);
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

static RMCTraverseLL traversell;
static RMCLockTraverseLL locktraversell;
static RMCUpdateLL updatell;

/* data path */
static constexpr std::array<std::pair<RMCType, RMCBase *>, NUM_REG_RMC>
    rmc_values{{std::make_pair(RMCUpdateLL::get_type(), &updatell)}};

/* data path */
static constexpr auto rmc_map =
    StaticMap<RMCType, RMCBase *, rmc_values.size()>{{rmc_values}};

/* control path */
CoroRMC rmcs_get_init(RMCType type, const MemoryRegion &mr) {
  return rmc_map.at(type)->runtime_init(mr);
}

/* data path */
CoroRMC rmcs_get_handler(const RMCType type, const BackendBase *b) {
  return rmc_map.at(type)->runtime_handler(b);
}

/* control path
   TODO: change to std::array<MemoryRegion, NUM_REG_RMC> & */
void rmcs_server_init(MrAllocator &sa, std::vector<MemoryRegion> &allocs) {
  for (auto &rmc_pair : rmc_values)
    allocs.push_back(rmc_pair.second->server_init(sa));
}
