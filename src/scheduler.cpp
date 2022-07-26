#include "scheduler.h"

void RMCScheduler::run() {
#if defined(BACKEND_DRAM) || defined(BACKEND_DRAM_COMP)
  rmcs_server_init(dram_allocator, allocs);
  backend.init(&dram_allocator, &allocs);
#else
  backend.init();
#endif

  RDMAClient &rclient = ns.onesidedclient.get_rclient();

  while (ns.nsready) {
#if defined(BACKEND_RDMA)
    schedule_interleaved(rclient);
#elif defined(BACKEND_DRAM) || defined(BACKEND_DRAM_COMP) || \
    defined(BACKEND_RDMA_COMP)
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

#if defined(BACKEND_DRAM)
  exec_interleaved_dram(server_ctx);
#elif defined(BACKEND_DRAM_COMP)
  exec_completion(server_ctx);
#endif

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
