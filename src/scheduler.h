#pragma once

//#define PERF_STATS

#include <inttypes.h>
#include <rdma/rdma_cma.h>

#include <algorithm>
#include <cassert>
#include <cstring>
#include <deque>
#include <vector>

#ifdef PERF_STATS
#include <numeric>
#endif

#include "corormc.h"
#include "nicserver.h"
#include "rmcs.h"
#include "rpc.h"

class NICServer;

/* one RMCScheduler per NIC core */
class RMCScheduler {
  using coro_handle = std::coroutine_handle<CoroRMC::promise_type>;
  NICServer &ns;

#if defined(BACKEND_RDMA)
  CoopRDMA backend;
#elif defined(BACKEND_DRAM)
  DramMrAllocator dram_allocator;
  std::vector<MemoryRegion> allocs;
  PrefetchDRAM backend;
#elif defined(BACKEND_DRAM_COMP)
  DramMrAllocator dram_allocator;
  std::vector<MemoryRegion> allocs;
  CompDRAM backend;
#elif defined(BACKEND_RDMA_COMP)
  CompRDMA backend;
#else
  static_assert(false, "Need to select a backend");
#endif

  /* RMCs ready to be run. Accessed directly by RMCLock */
  std::deque<std::coroutine_handle<>> runqueue;

  // num_qps per thread
  uint16_t num_qps;
  unsigned int req_idx = 0;
  uint32_t reply_idx = 0;
  bool pending_replies = false;
  /* true if we received a disconnect req, so we are waiting for rmcs to
     finish executing before disconnecting */
  bool recvd_disconnect = false;

  void req_exec_rmc(const DataReq *req);
  void req_init_rmc(const DataReq *req);
  void exec_interleaved(RDMAClient &rclient, RDMAContext &server_ctx);
  void exec_interleaved_dram(RDMAContext &server_ctx);
  void exec_completion(RDMAContext &server_ctx);
  void add_reply(coro_handle &rmc, RDMAContext &server_ctx);
  void send_poll_replies(RDMAContext &server_ctx);
  void check_new_reqs_client(RDMAContext &server_ctx);
  void poll_comps_host(RDMAClient &rclient);
  bool executing(RDMAClient &rclient) {
    if (!runqueue.empty()) return true;

    if (!rclient.memqueues_empty()) return true;

    return false;
  }
  /* Resumes an rmc's continuation if it is defined, otherwise resumes the
   * passed rmc. */
  void resume_coro(coro_handle &rmc) {
    if (rmc.promise().continuation) {
      /* a blocked continuation mustn't be in the runqueue */
      assert(!rmc.promise().continuation.promise().blocked);
      rmc.promise().continuation.resume();
    } else {
      rmc.resume();
    }
  }

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
  /* changing the next variable significantly alters the dynamics of
   * admission in the scheduler; be careful */
  static constexpr int MAX_NEW_REQS_PER_ITER = 24;
  static constexpr int MAX_HOSTMEM_POLL = 4;
  static constexpr int DEBUG_VEC_RESERVE = 1000000;
  /* only affects DRAM backends  */
  static constexpr uint16_t DRAM_MAX_EXECS_COMPLETION = 16;
  /* a smaller amount will cut the batch short (e.g., for exps) */
  static constexpr uint16_t MAX_HOSTMEM_BSIZE = QP_MAX_1SIDED_WRS;

  RMCScheduler(NICServer &nicserver, uint16_t num_qps)
      : ns(nicserver), backend(ns.onesidedclient), num_qps(num_qps) {
    printf("RMCScheduler batchsize=%u num_qps=%u tid=%u\n", MAX_HOSTMEM_BSIZE,
           num_qps, current_tid);
  }

  void run();
  void schedule_interleaved(RDMAClient &rclient);
  void schedule_completion(RDMAClient &rclient);
  void dispatch_new_req(DataReq *req);

  // used for communication to client, returns context[0] for all threads,
  // but since we have per-thread rserver, this is safe.
  RDMAContext &get_server_context() { return ns.rserver.get_ctrl_ctx(); }

  void debug_capture_stats();
  void debug_allocate();
  void debug_print_stats();
};

inline void RMCScheduler::req_exec_rmc(const DataReq *req) {
  assert(req->type == DataCmdType::CALL_RMC);

#ifdef PERF_STATS
  long long cycles_coros = get_cycles();
#endif

  const ExecReq *execreq = &req->data.exec;
  CoroRMC rmc = rmcs_get_handler(execreq->id, &backend, execreq);
  runqueue.push_back(rmc.get_handle());

#ifdef PERF_STATS
  debug_cycles_newcoros += get_cycles() - cycles_coros;
#endif
}

inline void RMCScheduler::req_init_rmc(const DataReq *req) {
  assert(req->type == DataCmdType::INIT_RMC);

  const InitReq *initreq = &req->data.init;

  // TODO: for multiple registered RMCs, get per-RMC ibv_mr or similar here
  // based on initreq->id
#if defined(BACKEND_DRAM) || defined(BACKEND_DRAM_COMP)
  CoroRMC initrmc = rmcs_get_init(initreq->id, allocs[0], &runqueue);
#else
  CoroRMC initrmc =
      rmcs_get_init(initreq->id, ns.onesidedclient.get_server_mr(), &runqueue);
#endif

  runqueue.push_back(initrmc.get_handle());
}

inline void RMCScheduler::dispatch_new_req(DataReq *req) {
  switch (req->type) {
    case DataCmdType::CALL_RMC:
      return req_exec_rmc(req);
    case DataCmdType::INIT_RMC:
      return req_init_rmc(req);
    case DataCmdType::LAST_CMD:
      this->recvd_disconnect = true;
#ifdef PERF_STATS
      debug_start = false;
#endif
      return;
    default:
      die("unrecognized DataReq type\n");
  }
}

inline void RMCScheduler::exec_interleaved(RDMAClient &rclient,
                                           RDMAContext &server_ctx) {
  auto resumes_left = runqueue.size();

  for (auto qp = 0u; qp < num_qps && resumes_left > 0; ++qp) {
    RDMAContext &clientctx = rclient.get_next_context();
    bool batch_started = false;

    while (resumes_left > 0 && clientctx.free_onesided_slots() > 0) {
      if (!batch_started) {
        rclient.start_batched_ops(&clientctx);
        batch_started = true;
      }

      coro_handle rmc = coro_handle::from_address(runqueue.front().address());
      runqueue.pop_front();

      resume_coro(rmc);
      resumes_left--;

      if (rmc.promise().waiting_mem_access)
        clientctx.memqueue.push(rmc);
      else if (rmc.done())
        add_reply(rmc, server_ctx);
      else {
        assert(rmc.promise().is_blocked());
        /* this is a blocked continuation, don't add it back to runqueue yet */
      }

#ifdef PERF_STATS
      debug_rmcexecs++;
#endif
      if (clientctx.newbatch_ops >= MAX_HOSTMEM_BSIZE) {
        rclient.end_batched_ops();
        batch_started = false;
      }
    }

    if (batch_started) rclient.end_batched_ops();
  }
}

inline void RMCScheduler::exec_interleaved_dram(RDMAContext &server_ctx) {
  thread_local std::array<std::coroutine_handle<>, DRAM_MAX_EXECS_COMPLETION>
      reordervec;
  uint8_t completions = 0;

  while (!runqueue.empty() && completions < DRAM_MAX_EXECS_COMPLETION) {
    bool prefetching = true;
    uint8_t resumes = 0;

  again:
    resumes = 0;

    while (!runqueue.empty() && resumes < DRAM_MAX_EXECS_COMPLETION) {
      coro_handle rmc = coro_handle::from_address(runqueue.front().address());
      runqueue.pop_front();

      resume_coro(rmc);

      if (rmc.promise().is_blocked())
        continue;
      else if (!rmc.done())
        /* TODO: not sure if resumes++ here is correct when the resumed rmc got
         * blocked */
        reordervec[resumes++] = rmc;
      else {
        add_reply(rmc, server_ctx);
        completions++;
      }
    }

    for (auto i = resumes - 1; i >= 0; i--) runqueue.push_front(reordervec[i]);

    if (prefetching) {
      prefetching = false;
      goto again;
    }
  }
}

inline void RMCScheduler::exec_completion(RDMAContext &server_ctx) {
  uint8_t execs = 0;

  while (!runqueue.empty() && execs < DRAM_MAX_EXECS_COMPLETION) {
    coro_handle rmc = coro_handle::from_address(runqueue.front().address());
    runqueue.pop_front();

    rmc.resume();

    add_reply(rmc, server_ctx);
    execs++;
  }

#ifdef PERF_STATS
  debug_rmcexecs += execs;
#endif
}

inline void RMCScheduler::add_reply(coro_handle &rmc, RDMAContext &server_ctx) {
#ifdef PERF_STATS
  long long cycles = get_cycles();
#endif
  CoroRMC::promise_type &promise = rmc.promise();

  if (!pending_replies) server_ctx.start_batch();

  pending_replies = true;
  DataReply *reply = ns.get_reply(this->reply_idx);
  inc_with_wraparound(this->reply_idx, QP_MAX_2SIDED_WRS);

  /* copies a generic reply to a buffer that we can post to rdma
     TODO: give RMCs a registered reply buffer directly */
  if (promise.reply_sz > 0) {
    reply->size = promise.reply_sz;
    std::memcpy(reply->data, promise.reply_ptr, promise.reply_sz);
  }

  ns.post_batched_send_reply(server_ctx, reply);
  rmc.destroy();

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
  thread_local auto update_out_sends = [&](size_t batch_size) {
    server_ctx.outstanding_sends -= batch_size;
  };

  if (pending_replies) {
    /* finish the batch */
    server_ctx.end_batch();
    pending_replies = false;
  }

  do {
    /* Poll. Ideally we should poll_atleast() if outstanding_sends ==
     * QP_MAX_2SIDED_WRS, but batched polls don't support atleast yet. */
    ns.rserver.poll_batched_atmost(1, ns.rserver.get_send_compqueue(0),
                                   update_out_sends);
  } while (server_ctx.outstanding_sends >= QP_MAX_2SIDED_WRS);

#ifdef PERF_STATS
  debug_cycles_replies += get_cycles() - cycles;
#endif
}

inline void RMCScheduler::check_new_reqs_client(RDMAContext &server_ctx) {
  int new_reqs;
  DataReq *req;

#ifdef PERF_STATS
  long long cycles = get_cycles();
#endif

  if (this->recvd_disconnect) return;

  new_reqs = ns.rserver.poll_batched_atmost(MAX_NEW_REQS_PER_ITER,
                                            ns.rserver.get_recv_compqueue(0),
                                            [](size_t) constexpr->void{});

#ifdef PERF_STATS
  if (!debug_start && new_reqs > 0) debug_start = true;
#endif
  if (new_reqs > 0) {
    auto prev_req_idx = this->req_idx;
    for (auto i = 0; i < new_reqs; ++i) {
      req = ns.get_req(this->req_idx);
      dispatch_new_req(req);
      inc_with_wraparound(this->req_idx, QP_MAX_2SIDED_WRS);
    }

    /* if true, it means we've wrapped around the req buffers.
       so issue two calls, one for the remaining of the buffer, and another one
       for the wrapped around reqs */
    if (new_reqs + prev_req_idx > QP_MAX_2SIDED_WRS) {
      ns.post_batched_recv_req(server_ctx, prev_req_idx,
                               QP_MAX_2SIDED_WRS - prev_req_idx);
      ns.post_batched_recv_req(server_ctx, 0,
                               new_reqs - (QP_MAX_2SIDED_WRS - prev_req_idx));
    } else {
      ns.post_batched_recv_req(server_ctx, prev_req_idx, new_reqs);
    }
  }
#ifdef PERF_STATS
  debug_reqs = new_reqs;
  debug_cycles_newreqs = get_cycles() - cycles;
#endif
}

inline void RMCScheduler::poll_comps_host(RDMAClient &rclient) {
#ifdef PERF_STATS
  long long cycles = get_cycles();
#endif
  uint16_t rmc_comps = 0;
  thread_local std::array<std::coroutine_handle<>,
                          MAX_HOSTMEM_BSIZE * MAX_HOSTMEM_POLL>
      reordervec;

  auto add_to_reordervec = [&rclient, &rmc_comps](size_t val) {
    const uint32_t ctx_id = val >> 32;
    const uint32_t batch_size = val & 0xFFFFFFFF;
    RDMAContext &ctx = rclient.get_context(ctx_id);
    // reorder completions
    for (auto i = 0u; i < batch_size; ++i) {
      coro_handle rmc =
          coro_handle::from_address(ctx.memqueue.front().address());

      /* multi ops can only come from a continuation (not from the root rmc). */
      if (rmc.promise().continuation &&
          rmc.promise().continuation.promise().multi_ops > 0) {
        /* we only pop an rmc from the memqueue if its multi_op counter is 0
         * (otherwise, we would resume it before all its accesses have
         * completed). */
        if (--rmc.promise().continuation.promise().multi_ops == 0) {
          reordervec[rmc_comps++] = rmc;
          ctx.memqueue.pop();
        }
      } else {
        reordervec[rmc_comps++] = rmc;
        ctx.memqueue.pop();
      }
    }

    ctx.complete_onesided(batch_size);
  };

  // poll up to MAX_HOSTMEM_BSIZE * MAX_HOSTMEM_POLL cqes
  int comps =
      ns.onesidedclient.poll_reads_atmost(MAX_HOSTMEM_POLL, add_to_reordervec);
  (void)comps;

  for (auto i = rmc_comps - 1; i >= 0; i--) runqueue.push_front(reordervec[i]);

#ifdef PERF_STATS
  debug_cycles_hostcomps = get_cycles() - cycles;
  debug_hostcomps = comps;
#endif
}

inline void RMCScheduler::debug_capture_stats() {
#ifdef PERF_STATS
  if (!debug_start) return;

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
