#ifndef RDMA_PEER_H
#define RDMA_PEER_H

#include <infiniband/mlx5dv.h>
#include <infiniband/verbs.h>
#include <netdb.h>
#include <rdma/rdma_cma.h>

#include <cassert>
#include <coroutine>
#include <list>
#include <memory>
#include <queue>
#include <vector>

#include "utils/utils.h"

class CoroRMC;
struct RDMAContext;

static constexpr uint32_t QP_MAX_2SIDED_WRS = 512;
static constexpr uint32_t QP_MAX_1SIDED_WRS = 16;
static constexpr uint32_t CQ_MAX_OUTSTANDING_CQES = 32;
/* TODO: try larger values of batched recvs */
static constexpr uint32_t MAX_BATCHED_RECVS = 16;
/* TODO: find a better place for this */
inline thread_local uint16_t current_tid;

/* wraps ibv_cq_ex's with batched polling */
struct CompQueue {
  ibv_cq_ex *cqx = nullptr;
  bool poll_started = false;
  unsigned int outstanding_cqes = 0;

  int start_poll() {
    assert(!poll_started);
    struct ibv_poll_cq_attr cq_attr = {};

    int ret = ibv_start_poll(cqx, &cq_attr);
    switch (ret) {
      case 0:
      case ENOENT:
        poll_started = true;
        return ret;
      default:
        die("ibv_start_poll() returned %d\n", ret);
    }
  }

  void maybe_end_poll() {
    if (!poll_started) return;

    if (outstanding_cqes > CQ_MAX_OUTSTANDING_CQES) {
      ibv_end_poll(cqx);
      outstanding_cqes = 0;
      poll_started = false;
    }
  }
};

class RDMAPeer {
 protected:
  std::vector<RDMAContext> contexts;
  ibv_context *dev_ctx;
  ibv_pd *pd;

  // cqs are distributed evenly across qps
  // e.g., if num_qps=4 and num_cqs=2, cq=0 is assigned to qp=0 and qp=1
  // and cq=1 is assigned to qp=2 and qp=3
  std::unique_ptr<CompQueue[]> send_cqs;
  std::unique_ptr<CompQueue[]> recv_cqs;
  std::vector<RDMAContext *> curr_batch_ctxs;

  bool pds_cqs_created = false;
  uint32_t unsignaled_sends = 0;
  uint16_t num_created_qps = 0;
  uint16_t num_qps;
  uint16_t num_cqs;
  uint16_t qps_per_thread;

  std::list<ibv_mr *> registered_mrs;

  void create_pds_cqs(ibv_context *verbs, bool onesided);
  void destroy_pds_cqs();
  void create_qps(RDMAContext &ctx, bool onesided);
  void connect_or_accept(RDMAContext &ctx, bool connect);
  void dereg_mrs();
  void handle_conn_established(RDMAContext &ctx);

 public:
  static constexpr int TIMEOUT_MS = 5;
  static constexpr int QP_ATTRS_MAX_SGE_ELEMS = 1;
  static constexpr int QP_ATTRS_MAX_INLINE_DATA = 256;
  static constexpr uint32_t MAX_UNSIGNALED_SENDS = 128;
  static constexpr int MAX_QP_INFLIGHT_READS = 16;  // hw limited

  RDMAPeer(uint16_t num_qps, uint16_t num_cqs)
      : send_cqs(std::unique_ptr<CompQueue[]>(new CompQueue[num_cqs])),
        recv_cqs(std::unique_ptr<CompQueue[]>(new CompQueue[num_cqs])),
        curr_batch_ctxs(num_cqs),
        num_qps(num_qps),
        num_cqs(num_cqs),
        qps_per_thread(num_qps / num_cqs) {
    static_assert(MAX_UNSIGNALED_SENDS < QP_MAX_2SIDED_WRS);
    assert(num_qps % num_cqs == 0);
    assert(qps_per_thread > 0);
  }

  virtual ~RDMAPeer() {
    for (ibv_mr *mr : registered_mrs) ibv_dereg_mr(mr);
    registered_mrs.clear();
  }

  RDMAPeer(const RDMAPeer &) = delete;
  RDMAPeer &operator=(const RDMAPeer &) = delete;
  RDMAPeer(RDMAPeer &&source) = delete;

  ibv_mr *register_mr(void *addr, size_t len, int permissions);
  void post_recv(const RDMAContext &ctx, const void *laddr, uint32_t len,
                 uint32_t lkey) const;
  void post_batched_recv(RDMAContext &ctx, ibv_mr *mr, uint32_t startidx,
                         uint32_t per_buf_bytes, uint32_t num_bufs) const;
  void post_send(const RDMAContext &ctx, const void *laddr, uint32_t len,
                 uint32_t lkey) const;
  /* posts an unsignaled 2-sided send,
     returns whether send_cqx should be polled */
  bool post_2s_send_unsig(const RDMAContext &ctx, const void *laddr,
                          uint32_t len, uint32_t lkey);
  void post_batched_send(RDMAContext &ctx, const void *laddr, uint32_t len,
                         uint32_t lkey);

  template <typename T>
  unsigned int poll_atleast(unsigned int times, ibv_cq_ex *cq, T &&comp_func);
  void poll_exactly(unsigned int times, ibv_cq_ex *cq);

  template <typename T>
  unsigned int poll_atmost(unsigned int max, ibv_cq_ex *cq, T &&comp_func);
  template <typename T>
  unsigned int poll_batched_atmost(unsigned int max, CompQueue &comp_queue,
                                   T &&comp_func);
  // these functions receive tids because some users (e.g., RDMAServer) are
  // created per thread, so they only have 1 cq. in those cases, indexing the cq
  // by thread id would be wrong.
  ibv_cq_ex *get_send_cq(uint16_t tid);
  ibv_cq_ex *get_recv_cq(uint16_t tid);
  CompQueue &get_send_compqueue(uint16_t tid);
  CompQueue &get_recv_compqueue(uint16_t tid);

  std::vector<RDMAContext> &get_contexts();
  RDMAContext &get_next_context();
  RDMAContext &get_context(uint16_t ctx_id);
  RDMAContext &get_ctrl_ctx();
  uint16_t get_num_qps();

  void start_batched_ops(RDMAContext *ctx);
  void end_batched_ops();
  RDMAContext *get_batch_ctx();
  bool memqueues_empty();
};

/* TODO: move this to own file */
struct RDMAContext {
  using coro_handle = std::coroutine_handle<>;

  struct SendOp {
    uint64_t wr_id;
    const void *laddr;
    unsigned int len;
    unsigned int lkey;
    bool signaled;
  };

  struct OneSidedOp {
    enum class OpType { INVALID, READ, WRITE, CMP_SWP, FETCH_ADD };

    uintptr_t raddr = 0;
    uintptr_t laddr = 0;
    uint32_t len = 0;
    uint32_t rkey = 0;
    uint32_t lkey = 0;
    uint64_t cmp = 0;
    uint64_t swp = 0;
    OpType optype = OpType::INVALID;

    void post(ibv_qp_ex *qpx, unsigned int flags, uint64_t wr_id) {
      qpx->wr_flags = flags;
      qpx->wr_id = wr_id;

      switch (optype) {
        case OpType::READ:
          ibv_wr_rdma_read(qpx, rkey, raddr);
          break;
        case OpType::WRITE:
          ibv_wr_rdma_write(qpx, rkey, raddr);
          break;
        case OpType::CMP_SWP:
          ibv_wr_atomic_cmp_swp(qpx, rkey, raddr, cmp, swp);
          break;
        case OpType::FETCH_ADD:
        case OpType::INVALID:
          die("bad optype\n");
      }

      ibv_wr_set_sge(qpx, lkey, laddr, len);
    }
  };

  struct RecvOp {
    ibv_recv_wr wr;
    ibv_sge sge;
  };

  enum BatchType { SEND, ONESIDED };

  /* TODO: move this inside SendOp */
  void post_send(SendOp &sendop) {
    if (sendop.signaled)
      qpx->wr_flags = IBV_SEND_SIGNALED;
    else
      qpx->wr_flags = 0;

    qpx->wr_id = sendop.wr_id;
    ibv_wr_send(qpx);
    ibv_wr_set_sge(qpx, sendop.lkey, (uintptr_t)sendop.laddr, sendop.len);
    outstanding_sends++;
  }

 public:
  uint32_t ctx_id; /* TODO: rename to id */
  bool connected = false;
  uint32_t outstanding_sends = 0;
  uint32_t curr_batch_size = 0;

  rdma_cm_id *cm_id = nullptr;
  ibv_qp *qp = nullptr;
  ibv_qp_ex *qpx = nullptr;
  rdma_event_channel *event_channel = nullptr;

  SendOp buffered_send;
  OneSidedOp buffered_onesided;
  BatchType curr_batch_type;

  std::queue<coro_handle> memqueue;
  std::vector<RecvOp> recv_batch;

  RDMAContext(unsigned int ctx_id)
      : ctx_id{ctx_id}, buffered_send{0, nullptr, 0, 0, false} {
    recv_batch.reserve(MAX_BATCHED_RECVS);
    for (auto i = 0u; i < MAX_BATCHED_RECVS; ++i) {
      recv_batch.emplace_back(RecvOp());
    }
  }

  void disconnect() {
    assert(connected);
    connected = false;

    ibv_destroy_qp(qp);
    rdma_destroy_id(cm_id);
    rdma_destroy_event_channel(event_channel);
  }

  void post_batched_send(const void *laddr, unsigned int len,
                         unsigned int lkey) {
    rt_assert(outstanding_sends < QP_MAX_2SIDED_WRS, "outstanding_sends=%u\n",
              outstanding_sends);

    /* if this is not the first WR within the batch, post the previously
     * buffered send */
    if (curr_batch_size > 0)
      post_send(buffered_send);
    else
      curr_batch_type = BatchType::SEND;

    buffered_send.wr_id = 0;
    buffered_send.laddr = laddr;
    buffered_send.len = len;
    buffered_send.lkey = lkey;
    buffered_send.signaled = false;

    curr_batch_size++;
  }

  /* post the last send of a batch */
  void end_batched_sends() {
    buffered_send.signaled = true;
    buffered_send.wr_id = curr_batch_size;
    post_send(buffered_send);
  }

  void post_batched_onesided(uintptr_t raddr, uintptr_t laddr, uint32_t size,
                             uint32_t rkey, uint32_t lkey,
                             OneSidedOp::OpType optype, uint64_t cmp,
                             uint64_t swp) {
    if (curr_batch_size > 0)
      buffered_onesided.post(qpx, 0, 0);
    else
      curr_batch_type = BatchType::ONESIDED;

    buffered_onesided.raddr = raddr;
    buffered_onesided.laddr = laddr;
    buffered_onesided.len = size;
    buffered_onesided.rkey = rkey;
    buffered_onesided.lkey = lkey;
    buffered_onesided.optype = optype;

    if (optype == OneSidedOp::OpType::CMP_SWP) {
      buffered_onesided.cmp = cmp;
      buffered_onesided.swp = swp;
    }

    curr_batch_size++;
  }

  void post_batched_onesided(OneSidedOp op) {
    if (curr_batch_size > 0)
      buffered_onesided.post(qpx, 0, 0);
    else
      curr_batch_type = BatchType::ONESIDED;

    buffered_onesided = op;
    curr_batch_size++;
  }

  void end_batched_onesided() {
    uint64_t wr_id = ctx_id;
    /* left 32 bits used for ctx_id, right 32 bits batch size */
    wr_id <<= 32;
    wr_id |= curr_batch_size;
    buffered_onesided.post(qpx, IBV_SEND_SIGNALED, wr_id);
  }

  /* arguments:
         laddr is the starting local address
         req_len is the individual recv request length in bytes
         total_reqs is the total number of recv reqs that will be posted
         lkey is the local key for the memory region the recv will access
  */
  void post_batched_recv(uintptr_t laddr, uint32_t req_len, uint32_t total_reqs,
                         uint32_t lkey) {
    ibv_recv_wr *bad_wr = nullptr;
    int err = 0;

    rt_assert(total_reqs <= MAX_BATCHED_RECVS, "total_reqs=%u\n", total_reqs);

    for (auto i = 0u; i < total_reqs; ++i) {
      recv_batch[i].sge.addr = laddr + i * req_len;
      recv_batch[i].sge.length = req_len;
      recv_batch[i].sge.lkey = lkey;

      recv_batch[i].wr.sg_list = &recv_batch[i].sge;
      recv_batch[i].wr.num_sge = 1;

      if (i == total_reqs - 1)
        recv_batch[i].wr.next = nullptr;
      else
        recv_batch[i].wr.next = &recv_batch[i + 1].wr;
    }

    err = ibv_post_recv(qp, &recv_batch[0].wr, &bad_wr);
    rt_assert(err == 0, "ibv_post_recv() returned %d\n", err);
  }

  void start_batch() {
    assert(curr_batch_size == 0);

    ibv_wr_start(qpx);
  }

  void end_batch() {
    /* if we are in the middle of a batched op, end it */
    if (curr_batch_size > 0) {
      switch (curr_batch_type) {
        case BatchType::SEND:
          end_batched_sends();
          break;
        case BatchType::ONESIDED:
          end_batched_onesided();
          break;
        default:
          die("unrecognized batch type\n");
      }
    }

    TEST_NZ(ibv_wr_complete(qpx));
    curr_batch_size = 0;
  }
};

inline ibv_mr *RDMAPeer::register_mr(void *addr, size_t len, int permissions) {
  ibv_mr *mr = ibv_reg_mr(pd, addr, len, permissions);
  if (!mr) die("could not register mr\n");

  registered_mrs.push_back(mr);
  return mr;
}

inline void RDMAPeer::post_recv(const RDMAContext &ctx, const void *laddr,
                                uint32_t len, uint32_t lkey) const {
  int err = 0;
  ibv_sge sge = {
      .addr = reinterpret_cast<uintptr_t>(laddr), .length = len, .lkey = lkey};

  ibv_recv_wr wr = {};
  ibv_recv_wr *bad_wr = nullptr;

  wr.next = nullptr;
  wr.sg_list = &sge;
  wr.num_sge = 1;

  err = ibv_post_recv(ctx.qp, &wr, &bad_wr);
  rt_assert(err == 0, "ibv_post_recv() returned %d\n", err);
}

inline void RDMAPeer::post_batched_recv(RDMAContext &ctx, ibv_mr *mr,
                                        uint32_t startidx,
                                        uint32_t per_buf_bytes,
                                        uint32_t num_bufs) const {
  uint32_t max_batch_size = MAX_BATCHED_RECVS;
  uint32_t num_batches = num_bufs / max_batch_size;
  uint32_t batch_size = 0;
  uintptr_t base_addr =
      reinterpret_cast<uintptr_t>(mr->addr) + (startidx * per_buf_bytes);

  if (num_bufs % max_batch_size != 0) num_batches++;

  for (auto batch = 0u; batch < num_batches; ++batch) {
    if (num_bufs > (batch + 1) * max_batch_size)
      batch_size = max_batch_size;
    else
      batch_size = num_bufs - (batch * max_batch_size);

    ctx.post_batched_recv(base_addr + (batch * max_batch_size * per_buf_bytes),
                          per_buf_bytes, batch_size, mr->lkey);
  }
}

inline void RDMAPeer::post_send(const RDMAContext &ctx, const void *laddr,
                                uint32_t len, uint32_t lkey) const {
  ibv_wr_start(ctx.qpx);
  ctx.qpx->wr_flags = IBV_SEND_SIGNALED;
  ibv_wr_send(ctx.qpx);
  ibv_wr_set_sge(ctx.qpx, lkey, reinterpret_cast<uintptr_t>(laddr), len);
  TEST_NZ(ibv_wr_complete(ctx.qpx));
}

/* returns whether the current posted send was signaled.
   the caller must make sure that we only attempt polling the
   signaled send after it has been posted */
inline bool RDMAPeer::post_2s_send_unsig(const RDMAContext &ctx,
                                         const void *laddr, uint32_t len,
                                         uint32_t lkey) {
  bool signaled = false;
  int ret;

  if (this->unsignaled_sends + 1 == MAX_UNSIGNALED_SENDS) signaled = true;

  ibv_wr_start(ctx.qpx);

  if (signaled)
    ctx.qpx->wr_flags = IBV_SEND_SIGNALED;
  else
    ctx.qpx->wr_flags = 0;

  ibv_wr_send(ctx.qpx);
  ibv_wr_set_sge(ctx.qpx, lkey, (uintptr_t)laddr, len);

  ret = ibv_wr_complete(ctx.qpx);
  rt_assert(ret == 0, "ibv_wr_complete() returned %d\n", ret);

  inc_with_wraparound(this->unsignaled_sends, MAX_UNSIGNALED_SENDS);
  return signaled;
}

inline ibv_cq_ex *RDMAPeer::get_send_cq(uint16_t tid) {
  return get_send_compqueue(tid).cqx;
}

inline ibv_cq_ex *RDMAPeer::get_recv_cq(uint16_t tid) {
  return get_recv_compqueue(tid).cqx;
}

inline CompQueue &RDMAPeer::get_send_compqueue(uint16_t tid) {
  assert(tid <= num_cqs);
  return send_cqs[tid];
}

inline CompQueue &RDMAPeer::get_recv_compqueue(uint16_t tid) {
  assert(tid <= num_cqs);
  return recv_cqs[tid];
}

inline std::vector<RDMAContext> &RDMAPeer::get_contexts() { return contexts; }

// this could potentially allow any thread to get any context, so be careful
// when using ctx_ids to select the context
inline RDMAContext &RDMAPeer::get_context(uint16_t ctx_id) {
  return contexts[ctx_id];
}

// if qps_per_thread == 1, always returns the tid-th context
// if qps_per_thread > 1, alternates between
//   [qps_per_thread * tid, qps_per_thread * tid + 1,...,qps_per_thread * tid +
//   (qps_per_thread - 1)]
inline RDMAContext &RDMAPeer::get_next_context() {
  thread_local uint16_t per_thread_idx = 0;

  if (qps_per_thread == 1) return contexts[current_tid];

  RDMAContext &ctx = contexts[qps_per_thread * current_tid + per_thread_idx];
  inc_with_wraparound(per_thread_idx, qps_per_thread);
  return ctx;
}

/* used for send/recvs. we could have an independent qp only for these ops
   but probably not worth the code */
inline RDMAContext &RDMAPeer::get_ctrl_ctx() { return contexts[0]; }

inline uint16_t RDMAPeer::get_num_qps() { return num_qps; }

template <typename T>
inline unsigned int RDMAPeer::poll_atmost(unsigned int max, ibv_cq_ex *cq,
                                          T &&comp_func) {
  int ret;
  unsigned int polled = 0;
  struct ibv_poll_cq_attr cq_attr = {};

  assert(max > 0);

  while ((ret = ibv_start_poll(cq, &cq_attr)) != 0) {
    if (ret == ENOENT)
      return 0;
    else
      die("ibv_start_poll() returned %d\n", ret);
  }

  do {
    if (polled > 0) {
      while ((ret = ibv_next_poll(cq)) != 0) {
        if (ret == ENOENT)
          goto end_poll;
        else
          die("ibv_next_poll() returned %d\n", ret);
      }
    }

    rt_assert(cq->status == IBV_WC_SUCCESS, "poll_atmost() cqe->status=%d\n",
              cq->status);

    /* the post-completion function takes wr_id,
     * for reads, this is the ctx_id,
     * for sends, this is the batch size */
    comp_func(cq->wr_id);

    polled++;
  } while (polled < max);

end_poll:
  ibv_end_poll(cq);
  assert(polled <= max);
  return polled;
}

template <typename T>
inline unsigned int RDMAPeer::poll_batched_atmost(unsigned int max,
                                                  CompQueue &comp_queue,
                                                  T &&comp_func) {
  int ret;
  unsigned int polled = 0;

  assert(max > 0);

  if (!comp_queue.poll_started) {
    ret = comp_queue.start_poll();
    if (ret == ENOENT)  // no comp available
      goto end_poll;
    else  // comp available, read it
      goto read;
  }

  do {
    ret = ibv_next_poll(comp_queue.cqx);
    if (ret == ENOENT)
      goto end_poll;
    else if (ret != 0)
      die("ibv_next_poll() returned %d\n", ret);

  read:
    rt_assert(comp_queue.cqx->status == IBV_WC_SUCCESS,
              "poll_batched_atmost() cqe->status=%d\n", comp_queue.cqx->status);

    /* the post-completion function takes wr_id,
     * for reads, this is the ctx_id,
     * for sends, this is the batch size */
    comp_func(comp_queue.cqx->wr_id);

    polled++;
  } while (polled < max);

end_poll:
  comp_queue.outstanding_cqes += polled;
  comp_queue.maybe_end_poll();
  assert(polled <= max);
  return polled;
}

inline void RDMAPeer::poll_exactly(unsigned int target, ibv_cq_ex *cq) {
  int ret;
  unsigned int polled = 0;
  struct ibv_poll_cq_attr cq_attr = {};

  while ((ret = ibv_start_poll(cq, &cq_attr)) != 0) {
    if (ret == ENOENT)
      continue;
    else
      die("ibv_start_poll() returned %d\n", ret);
  }

  do {
    if (polled > 0) {
      while ((ret = ibv_next_poll(cq)) != 0) {
        if (ret == ENOENT)
          continue;
        else
          die("ibv_next_poll() returned %d\n", ret);
      }
    }

    rt_assert(cq->status == IBV_WC_SUCCESS, "poll_exactly() cqe->status=%d\n",
              cq->status);
    polled++;
  } while (polled < target);

  ibv_end_poll(cq);
}

template <typename T>
inline unsigned int RDMAPeer::poll_atleast(unsigned int target, ibv_cq_ex *cq,
                                           T &&comp_func) {
  int ret;
  unsigned int polled = 0;
  struct ibv_poll_cq_attr cq_attr = {};

  while ((ret = ibv_start_poll(cq, &cq_attr)) != 0) {
    if (ret == ENOENT)
      continue;
    else
      die("error in ibv_start_poll()\n");
  }

read:
  if (cq->status != IBV_WC_SUCCESS) die("cq status is not success\n");

  /* the post-completion function takes wr_id,
   * for sends, this is the batch size */
  comp_func(cq->wr_id);

  polled++;

next_poll:
  ret = ibv_next_poll(cq);
  if (ret == 0) {
    goto read;
  } else if (ret == ENOENT) {
    if (polled < target)
      goto next_poll; /* we haven't reached the target, retry. */
    else
      goto out; /* reached target, we can leave */
  } else {
    die("error in ibv_next_poll()\n");
  }

out:
  ibv_end_poll(cq);
  return polled;
}

inline void RDMAPeer::start_batched_ops(RDMAContext *ctx) {
  assert(curr_batch_ctxs[current_tid] == nullptr);

  curr_batch_ctxs[current_tid] = ctx;
  ctx->start_batch();
}

/* end batched ops (reads/writes/sends) */
inline void RDMAPeer::end_batched_ops() {
  assert(curr_batch_ctxs[current_tid] != nullptr);

  curr_batch_ctxs[current_tid]->end_batch();
  curr_batch_ctxs[current_tid] = nullptr;
}

inline RDMAContext *RDMAPeer::get_batch_ctx() {
  return curr_batch_ctxs[current_tid];
}

inline bool RDMAPeer::memqueues_empty() {
  for (auto i = 0u; i < qps_per_thread; ++i) {
    RDMAContext &ctx = contexts[qps_per_thread * current_tid + i];
    if (!ctx.memqueue.empty()) return false;
  }

  return true;
}

#endif
