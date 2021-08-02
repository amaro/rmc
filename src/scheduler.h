#pragma once

//#define PERF_STATS

#include <cassert>
#include <cstdlib>
#include <deque>
#include <inttypes.h>
#include <iostream>
#include <rdma/rdma_cma.h>
#include <unordered_map>
#include <vector>

#ifdef PERF_STATS
#include <numeric>
#endif

#include "corormc.h"
#include "nicserver.h"
#include "rmc.h"

class NICServer;

/* one RMCScheduler per NIC core */
class RMCScheduler {
  using void_handle = std::coroutine_handle<>;
  using promise_handle = std::coroutine_handle<CoroRMC::promise_type>;
  NICServer &ns;

  std::unordered_map<RMCId, RMC> id_rmc_map;
  /* RMCs ready to be run */
  std::deque<void_handle> runqueue;

  size_t num_llnodes;
  uint16_t num_qps;
  unsigned int req_idx;
  uint32_t reply_idx;
  bool pending_replies;
  /* true if we received a disconnect req, so we are waiting for rmcs to
     finish executing before disconnecting */
  bool recvd_disconnect;

  RMCId get_rmc_id(const RMC &rmc);
  void req_get_rmc_id(CmdRequest *req);
  void req_new_rmc(CmdRequest *req);
  void add_reply(promise_handle rmc, RDMAContext &server_ctx);
  void send_poll_replies(RDMAContext &server_ctx);
  void check_new_reqs_client(RDMAContext &server_ctx);
  void poll_comps_host();
  bool executing();

#ifdef PERF_STATS
  bool debug_start = false;
  uint16_t debug_reqs = 0;
  uint16_t debug_replies = 0;
  uint16_t debug_rmcexecs = 0;
  uint16_t debug_hostcomps = 0;
  long long debug_cycles = 0;
  long long debug_cycles_newreqs = 0;
  long long debug_cycles_replies = 0;
  long long debug_cycles_execs = 0;
  long long debug_cycles_hostcomps = 0;
  long long debug_cycles_newcoros = 0;
  std::vector<long long> debug_vec_cycles;
  std::vector<long long> debug_vec_cycles_reqs;
  std::vector<long long> debug_vec_cycles_replies;
  std::vector<long long> debug_vec_cycles_execs;
  std::vector<long long> debug_vec_cycles_hostcomps;
  std::vector<uint16_t> debug_vec_reqs;
  std::vector<uint16_t> debug_vec_rmcexecs;
  std::vector<uint16_t> debug_vec_replies;
  std::vector<uint16_t> debug_vec_memqsize;
  std::vector<uint16_t> debug_vec_hostcomps;
#endif

public:
  static constexpr int MAX_NEW_REQS_PER_ITER = 32;
  /* decreasing MAX_HOSTMEM_BATCH_SIZE increases latency because
  there's more involvement from CPU to issue more batches and more host comps */
  static constexpr int MAX_HOSTMEM_BATCH_SIZE = 8;
  static constexpr int MAX_HOSTMEM_POLL = 4;
  static constexpr int DEBUG_VEC_RESERVE = 1000000;

  RMCScheduler(NICServer &nicserver, size_t num_nodes, uint16_t num_qps)
      : ns(nicserver), num_llnodes(num_nodes), num_qps(num_qps), req_idx(0),
        reply_idx(0), pending_replies(0), recvd_disconnect(false) {
    for (auto i = 0u; i < QP_MAX_2SIDED_WRS; ++i) {
      runcoros = true;
      spawn(traverse_linkedlist(num_llnodes));
    }
  }

  ~RMCScheduler() {
    runcoros = false;
    while (!freequeue.empty()) {
      auto coro = freequeue.front();
      freequeue.pop_front();
      coro.resume(); // coro gets destroyed
    }
  }

  void run();
  void schedule(RDMAClient &rclient);
  void set_num_llnodes(size_t num_nodes);
  void set_num_qps(uint16_t num_qps);
  void dispatch_new_req(CmdRequest *req);
  void spawn(CoroRMC coro);

  RDMAContext &get_server_context();
  RDMAContext &get_client_context(unsigned int id);
  RDMAContext &get_next_client_context();

  void debug_capture_stats();
  void debug_allocate();
  void debug_print_stats();
};

inline RMCId RMCScheduler::get_rmc_id(const RMC &rmc) {
  RMCId id = std::hash<RMC>{}(rmc);

  if (id_rmc_map.find(id) == id_rmc_map.end()) {
    id_rmc_map.insert({id, rmc});
    LOG("registered new id=" << id << "for rmc=" << rmc);
  }

  return id;
}

inline void RMCScheduler::set_num_llnodes(size_t num_nodes) {
  LOG("num nodes in each linked list=" << num_nodes);
  num_llnodes = num_nodes;
}

inline void RMCScheduler::set_num_qps(uint16_t num_qps) {
  this->num_qps = num_qps;
}

inline RDMAContext &RMCScheduler::get_server_context() {
  return ns.rserver.get_ctrl_ctx();
}

inline RDMAContext &RMCScheduler::get_client_context(unsigned int id) {
  return ns.onesidedclient.get_rclient().get_contexts()[id];
}

inline RDMAContext &RMCScheduler::get_next_client_context() {
  static uint16_t id = 0;

  if (num_qps == 1)
    return get_client_context(0);

  RDMAContext &ctx = get_client_context(id);
  inc_with_wraparound(id, num_qps);
  return ctx;
}

inline bool RMCScheduler::executing() {
  if (!runqueue.empty())
    return true;

  for (const auto &ctx : ns.onesidedclient.get_rclient().get_contexts()) {
    if (!ctx.memqueue.empty())
      return true;
  }

  return false;
}

inline void RMCScheduler::dispatch_new_req(CmdRequest *req) {
  switch (req->type) {
  case CmdType::GET_RMCID:
    return req_get_rmc_id(req);
  case CmdType::CALL_RMC:
    return req_new_rmc(req);
  case CmdType::LAST_CMD:
    this->recvd_disconnect = true;
#ifdef PERF_STATS
    debug_start = false;
#endif
    return;
  default:
    DIE("unrecognized CmdRequest type");
  }
}

inline void RMCScheduler::add_reply(promise_handle rmc,
                                    RDMAContext &server_ctx) {
#ifdef PERF_STATS
  long long cycles = get_cycles();
#endif

  if (!pending_replies)
    ns.rserver.start_batched_ops(&server_ctx);

  pending_replies = true;
  CmdReply *reply = ns.get_reply(this->reply_idx);

  *(reinterpret_cast<int *>(reply->reply.call.data)) = rmc.promise().reply_val;

  ns.post_batched_send_reply(server_ctx, reply);
  inc_with_wraparound(this->reply_idx, ns.bsize);

#ifdef PERF_STATS
  long long cycles_end = get_cycles();
  debug_replies++;
  debug_cycles_replies += cycles_end - cycles;
#endif
}

inline void RMCScheduler::send_poll_replies(RDMAContext &server_ctx) {
#ifdef PERF_STATS
  long long cycles = get_cycles();
#endif
  static auto update_out_sends = [&](size_t batch_size) {
    server_ctx.outstanding_sends -= batch_size;
  };

  if (pending_replies) {
    /* finish the batch */
    ns.rserver.end_batched_ops();
    pending_replies = false;
  }

  /* poll */
  ns.rserver.poll_batched_atmost(1, ns.rserver.get_send_compqueue(),
                                 update_out_sends);

#ifdef PERF_STATS
  debug_cycles_replies += get_cycles() - cycles;
#endif
}

inline void RMCScheduler::check_new_reqs_client(RDMAContext &server_ctx) {
  int new_reqs;
  CmdRequest *req;

#ifdef PERF_STATS
  long long cycles = get_cycles();
#endif

  if (this->recvd_disconnect)
    return;

  auto noop = [](size_t) constexpr->void{};
  new_reqs = ns.rserver.poll_batched_atmost(
      MAX_NEW_REQS_PER_ITER, ns.rserver.get_recv_compqueue(), noop);

#ifdef PERF_STATS
  if (!debug_start && new_reqs > 0)
    debug_start = true;
#endif
  if (new_reqs > 0) {
    auto prev_req_idx = this->req_idx;
    for (auto i = 0; i < new_reqs; ++i) {
      req = ns.get_req(this->req_idx);
      dispatch_new_req(req);
      inc_with_wraparound(this->req_idx, ns.bsize);
    }

    /* if true, it means we've wrapped around the req buffers.
       so issue two calls, one for the remaining of the buffer, and another one
       for the wrapped around reqs */
    if (new_reqs + prev_req_idx > ns.bsize) {
      ns.post_batched_recv_req(server_ctx, prev_req_idx,
                               ns.bsize - prev_req_idx);
      ns.post_batched_recv_req(server_ctx, 0,
                               new_reqs - (ns.bsize - prev_req_idx));
    } else {
      ns.post_batched_recv_req(server_ctx, prev_req_idx, new_reqs);
    }
  }
#ifdef PERF_STATS
  debug_reqs = new_reqs;
  debug_cycles_newreqs = get_cycles() - cycles;
#endif
}

inline void RMCScheduler::poll_comps_host() {
#ifdef PERF_STATS
  long long cycles = get_cycles();
#endif
  static auto add_to_runqueue = [this](size_t val) {
    const uint32_t ctx_id = val >> 32;
    const uint32_t batch_size = val & 0xFFFFFFFF;
    RDMAContext &ctx = this->get_client_context(ctx_id);
    for (auto i = 0u; i < batch_size; ++i) {
      this->runqueue.push_front(ctx.memqueue.front());
      ctx.memqueue.pop();
    }
  };

  /* poll up to MAX_HOSTMEM_BATCH_SIZE * MAX_HOSTMEM_POLL cqes */
  int comps =
      ns.onesidedclient.poll_reads_atmost(MAX_HOSTMEM_POLL, add_to_runqueue);
  (void)comps;
#ifdef PERF_STATS
  debug_cycles_hostcomps = get_cycles() - cycles;
  debug_hostcomps = comps;
#endif
}

inline void RMCScheduler::debug_capture_stats() {
#ifdef PERF_STATS
  if (!debug_start)
    return;

  debug_vec_cycles.push_back(debug_cycles);
  debug_vec_cycles_execs.push_back(debug_cycles_execs);
  debug_vec_cycles_reqs.push_back(debug_cycles_newreqs);
  debug_vec_cycles_replies.push_back(debug_cycles_replies);
  debug_vec_cycles_hostcomps.push_back(debug_cycles_hostcomps);
  debug_vec_reqs.push_back(debug_reqs);
  debug_vec_replies.push_back(debug_replies);
  debug_vec_rmcexecs.push_back(debug_rmcexecs);
  debug_vec_hostcomps.push_back(debug_hostcomps);

  unsigned int memq_size = 0;
  for (auto &ctx : ns.onesidedclient.get_rclient().get_contexts())
    memq_size += ctx.memqueue.size();
  debug_vec_memqsize.push_back(memq_size);

  debug_cycles = 0;
  debug_cycles_execs = 0;
  debug_cycles_newreqs = 0;
  debug_cycles_replies = 0;
  debug_cycles_hostcomps = 0;
  debug_reqs = 0;
  debug_replies = 0;
  debug_rmcexecs = 0;
  debug_hostcomps = 0;

  if (debug_vec_cycles.size() == DEBUG_VEC_RESERVE) {
    LOG("debug vector full, terminating");
    debug_print_stats();
    std::terminate();
  }
#endif
}
