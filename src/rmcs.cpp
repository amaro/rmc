#include "rmcs.h"

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
