#include "scheduler.h"

/* Compute the id for this rmc, if it doesn't exist, register it in map.
   Return the id */
void RMCScheduler::req_get_rmc_id(CmdRequest *req) {
  assert(ns.nsready);
  assert(req->type == CmdType::GET_RMCID);

  RMC rmc(req->request.getid.rmc);
  RMCId id = this->get_rmc_id(rmc);

  /* use buffer 0 for get rmc id reply */
  CmdReply *reply = ns.get_reply(0);
  reply->type = CmdType::GET_RMCID;
  reply->reply.getid.id = id;
  ns.post_send_reply(reply);
  ns.rserver.poll_exactly(1, ns.rserver.get_send_cq(0));
}

CoroRMC RMCScheduler::get_rmc(const CallReq *req) {
  switch (req->id) {
  case READ:
    return std::move(traverse_linkedlist(backend));
  case READ_LOCK:
    return std::move(lock_traverse_linkedlist(backend));
  case WRITE:
    return std::move(random_writes(backend));
#if defined(WORKLOAD_HASHTABLE)
  case HASHTABLE:
    thread_local uint8_t num_gets = 0;
    if (++num_gets > 1) {
      num_gets = 0;
      return std::move(hash_insert(backend));
    }

    return std::move(hash_lookup(backend));
    // thread_local uint16_t num_gets = 0;

    // if (num_gets > 20)
    //  return std::move(hash_lookup(backend));

    // num_gets++;
    // if (num_gets > 20)
    //  std::cout << "this is last insert\n";
    // return std::move(hash_insert(backend));
#endif
  default:
    die("invalid req id");
    return std::move(traverse_linkedlist(backend));
  }
}

void RMCScheduler::run() {
  backend.init();
  RDMAClient &rclient = ns.onesidedclient.get_rclient();

  while (ns.nsready) {
#if defined(BACKEND_RDMA)
    schedule_interleaved(rclient);
#elif defined(BACKEND_DRAM) || defined(BACKEND_RDMA_COMP)
    schedule_completion(rclient);
#endif

    if (this->recvd_disconnect && !this->executing(rclient))
      return ns.disconnect();

    debug_capture_stats();
  }
}

void RMCScheduler::schedule_interleaved(RDMAClient &rclient) {
#ifdef PERF_STATS
  long long exec_start = get_cycles();
#endif
  thread_local RDMAContext &server_ctx = get_server_context();

  exec_interleaved(rclient, server_ctx);
  send_poll_replies(server_ctx);
#ifdef PERF_STATS
  /* we do it here because add_reply() above computes its own duration
     that must be added to the time spent in send_poll_replies()
     both of them must be substracted from debug_cycles_execs */
  debug_cycles_execs = get_cycles() - exec_start - debug_cycles_replies;
#endif

  check_new_reqs_client(server_ctx);
  poll_comps_host(rclient);

#ifdef PERF_STATS
  debug_cycles = get_cycles() - exec_start;
#endif
}

/* does not poll for host completions */
void RMCScheduler::schedule_completion(RDMAClient &rclient) {
#ifdef PERF_STATS
  long long exec_start = get_cycles();
#endif
  thread_local RDMAContext &server_ctx = get_server_context();

  // use exec_completion for run-to-completion
  // exec_completion(server_ctx);
  // use exec_interleaved_dram for interleaved
  exec_interleaved_dram(server_ctx);
  send_poll_replies(server_ctx);
#ifdef PERF_STATS
  /* we do it here because add_reply() above computes its own duration
     that must be added to the time spent in send_poll_replies()
     both of them must be substracted from debug_cycles_execs */
  debug_cycles_execs = get_cycles() - exec_start - debug_cycles_replies;
#endif

  check_new_reqs_client(server_ctx);

#ifdef PERF_STATS
  debug_cycles = get_cycles() - exec_start;
#endif
}

void RMCScheduler::debug_print_stats() {
#ifdef PERF_STATS
  if (debug_vec_reqs.size() != debug_vec_replies.size()) {
    std::cout << "vec_reqs size=" << debug_vec_reqs.size() << "\n";
    std::cout << "vec_replies size=" << debug_vec_replies.size() << "\n";
  }

  std::cout << "reqs,replies,memq_size,execs,host_comps,cycle,cycle_reqs,cycle_"
               "replies,cycle_execs,cycle_hostcomps\n";
  for (auto i = 0u; i < debug_vec_reqs.size(); ++i) {
    std::cout << debug_vec_reqs[i] << "," << debug_vec_replies[i] << ","
              << debug_vec_memqsize[i] << "," << debug_vec_rmcexecs[i] << ","
              << debug_vec_hostcomps[i] << "," << debug_vec_cycles[i] << ","
              << debug_vec_cycles_reqs[i] << "," << debug_vec_cycles_replies[i]
              << "," << debug_vec_cycles_execs[i] << ","
              << debug_vec_cycles_hostcomps[i] << "\n";
  }

  std::cout << "total reqs="
            << std::accumulate(debug_vec_reqs.begin(), debug_vec_reqs.end(), 0)
            << "\n";
  std::cout << "total replies="
            << std::accumulate(debug_vec_replies.begin(),
                               debug_vec_replies.end(), 0)
            << "\n";
  std::cout << "total execs="
            << std::accumulate(debug_vec_rmcexecs.begin(),
                               debug_vec_rmcexecs.end(), 0)
            << "\n";
  std::cout << "total cycles new coros =" << debug_cycles_newcoros
            << " (substract from total cycles req)\n";
#endif
}

void RMCScheduler::debug_allocate() {
#ifdef PERF_STATS
  debug_vec_cycles.reserve(DEBUG_VEC_RESERVE);
  debug_vec_cycles_reqs.reserve(DEBUG_VEC_RESERVE);
  debug_vec_cycles_replies.reserve(DEBUG_VEC_RESERVE);
  debug_vec_cycles_execs.reserve(DEBUG_VEC_RESERVE);
  debug_vec_cycles_hostcomps.reserve(DEBUG_VEC_RESERVE);

  debug_vec_reqs.reserve(DEBUG_VEC_RESERVE);
  debug_vec_rmcexecs.reserve(DEBUG_VEC_RESERVE);
  debug_vec_replies.reserve(DEBUG_VEC_RESERVE);
  debug_vec_memqsize.reserve(DEBUG_VEC_RESERVE);
#endif
}
