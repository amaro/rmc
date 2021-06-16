#ifndef RDMA_PEER_H
#define RDMA_PEER_H

#include <list>
#include <vector>
#include <queue>
#include <cassert>

#include <netdb.h>
#include <rdma/rdma_cma.h>
#include <infiniband/verbs.h>
#include <infiniband/mlx5dv.h>
#include "utils/utils.h"

template <typename T> class CoroRMC;
struct RDMAContext;

struct CompQueue {
    ibv_cq_ex *cqx = nullptr;
    bool poll_started = false;
    unsigned int num_cqes_polled = 0;

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
            DIE("ibv_start_poll() returned=" << ret);
            return -1;
        }
    }

    void maybe_end_poll() {
        if (!poll_started)
            return;

        if (num_cqes_polled > 16) {
            ibv_end_poll(cqx);
            num_cqes_polled = 0;
            poll_started = false;
        }
    }
};

class RDMAPeer {
protected:
    std::vector<RDMAContext> contexts;
    ibv_context *dev_ctx;
    ibv_pd *pd;
    CompQueue send_cq;
    CompQueue recv_cq;

    bool pds_cqs_created;
    uint32_t unsignaled_sends;
    unsigned int num_qps;

    std::list<ibv_mr *> registered_mrs;

    void create_pds_cqs(ibv_context *verbs);
    void destroy_pds_cqs();
    void create_qps(RDMAContext &ctx);
    void connect_or_accept(RDMAContext &ctx, bool connect);
    void dereg_mrs();
    void handle_conn_established(RDMAContext &ctx);

public:
    static constexpr int CQ_NUM_CQE = 1024;
    static constexpr int TIMEOUT_MS = 5;
    static constexpr int QP_ATTRS_MAX_OUTSTAND_SEND_WRS = 1024;
    static constexpr int QP_ATTRS_MAX_OUTSTAND_RECV_WRS = 1024;
    static constexpr int QP_ATTRS_MAX_SGE_ELEMS = 1;
    static constexpr int QP_ATTRS_MAX_INLINE_DATA = 256;
    static constexpr uint32_t MAX_UNSIGNALED_SENDS = 256;
    static constexpr int MAX_QP_INFLIGHT_READS = 16; // hw limited

    RDMAContext *batch_ctx; /* TODO: this shouldn't be kept here, caller should maintain this */

    RDMAPeer(unsigned int num_qps) :
        pds_cqs_created(false), unsignaled_sends(0), num_qps(num_qps), batch_ctx(nullptr) {
        static_assert(CQ_NUM_CQE == QP_ATTRS_MAX_OUTSTAND_SEND_WRS);
        static_assert(QP_ATTRS_MAX_OUTSTAND_SEND_WRS == QP_ATTRS_MAX_OUTSTAND_RECV_WRS);
        static_assert(MAX_UNSIGNALED_SENDS < QP_ATTRS_MAX_OUTSTAND_SEND_WRS);
    }

    virtual ~RDMAPeer() { }

    ibv_mr *register_mr(void *addr, size_t len, int permissions);
    void post_recv(const RDMAContext &ctx, void *laddr,
                    uint32_t len, uint32_t lkey) const;
    void post_batched_recv(RDMAContext &ctx, ibv_mr *mr, uint32_t startidx,
                        uint32_t per_buf_bytes, uint32_t num_bufs) const;
    void post_send(const RDMAContext &ctx, void *laddr,
                    uint32_t len, uint32_t lkey) const;
    /* posts an unsignaled 2-sided send,
       returns whether send_cqx should be polled */
    bool post_2s_send_unsig(const RDMAContext &ctx, void *laddr,
                            uint32_t len, uint32_t lkey);
    void post_batched_send(RDMAContext &ctx, void *laddr,
                            uint32_t len, uint32_t lkey);

    template<typename T>
    unsigned int poll_atleast(unsigned int times, ibv_cq_ex *cq, T&& comp_func);
    void poll_exactly(unsigned int times, ibv_cq_ex *cq);

    template<typename T>
    unsigned int poll_atmost(unsigned int max, ibv_cq_ex *cq, T&& comp_func);
    template<typename T>
    unsigned int poll_batched_atmost(unsigned int max, CompQueue &comp_queue, T&& comp_func);
    ibv_cq_ex *get_send_cq();
    ibv_cq_ex *get_recv_cq();
    CompQueue &get_send_compqueue();
    CompQueue &get_recv_compqueue();

    std::vector<RDMAContext> &get_contexts();
    RDMAContext &get_ctrl_ctx();
    unsigned int get_num_qps();

    void start_batched_ops(RDMAContext *ctx);
    void end_batched_ops();
};

/* TODO: move this to own file */
struct RDMAContext {
    struct SendOp {
        void *laddr;
        unsigned int len;
        unsigned int lkey;
        bool signaled;
        unsigned long wr_id;
    };

    struct RecvOp {
        ibv_recv_wr wr;
        ibv_sge sge;
    };

    void post_send(SendOp &sendop) {
        if (sendop.signaled)
            qpx->wr_flags = IBV_SEND_SIGNALED;
        else
            qpx->wr_flags = 0;

        qpx->wr_id = sendop.wr_id;
        ibv_wr_send(qpx);

        ibv_wr_set_sge(qpx, sendop.lkey, (uintptr_t) sendop.laddr, sendop.len);

        outstanding_sends++;
    }

public:
    std::queue<CoroRMC<int> *> memqueue;
    std::vector<RecvOp> recv_batch;
    unsigned int ctx_id; /* TODO: rename to id */
    bool connected;
    unsigned int outstanding_sends;
    unsigned int curr_batch_size;

    rdma_cm_id *cm_id;
    ibv_qp *qp;
    ibv_qp_ex *qpx;
    rdma_event_channel *event_channel;
    SendOp buffered_send;

    static constexpr uint32_t MAX_BATCHED_RECVS = 16;

    RDMAContext(unsigned int ctx_id) :
        ctx_id{ctx_id}, connected{false}, outstanding_sends{0}, curr_batch_size{0},
        cm_id{nullptr}, qp{nullptr}, qpx{nullptr}, event_channel{nullptr},
        buffered_send{nullptr, 0, 0, false, 0} {

        for (auto i = 0u; i < MAX_BATCHED_RECVS; ++i) {
            recv_batch.push_back(RecvOp());
        }
    }

    void disconnect() {
        assert(connected);
        connected = false;

        ibv_destroy_qp(qp);
        rdma_destroy_id(cm_id);
        rdma_destroy_event_channel(event_channel);
    }

    void post_batched_send(void *laddr, unsigned int len, unsigned int lkey) {
        if(outstanding_sends >= RDMAPeer::QP_ATTRS_MAX_OUTSTAND_SEND_WRS)
            DIE("ctx.outstanding_sends=" << outstanding_sends);

        /* if this is not the first WR within the batch, post the previously buffered send */
        if (curr_batch_size > 0)
            post_send(buffered_send);

        buffered_send.laddr = laddr;
        buffered_send.len = len;
        buffered_send.lkey = lkey;
        buffered_send.signaled = false;
        buffered_send.wr_id = 0;
        curr_batch_size++;
    }

    /* post the last send of a batch */
    void end_batched_send() {
        buffered_send.signaled = true;
        buffered_send.wr_id = curr_batch_size;
        post_send(buffered_send);
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

        if (total_reqs > MAX_BATCHED_RECVS)
            DIE("total_reqs > MAX_BATCHED_RECVS");

        for (auto i = 0u; i < total_reqs; ++i) {
            recv_batch[i].sge.addr = ((uintptr_t) laddr) + i * req_len;
            recv_batch[i].sge.length = req_len;
            recv_batch[i].sge.lkey = lkey;

            recv_batch[i].wr.sg_list = &recv_batch[i].sge;
            recv_batch[i].wr.num_sge = 1;

            if (i == total_reqs - 1)
                recv_batch[i].wr.next = nullptr;
            else
                recv_batch[i].wr.next = &recv_batch[i + 1].wr;
        }

        if ((err = ibv_post_recv(qp, &recv_batch[0].wr, &bad_wr)) != 0)
            DIE("post_recv() returned " << err);
    }
};


inline ibv_mr *RDMAPeer::register_mr(void *addr, size_t len, int permissions)
{
    ibv_mr *mr = ibv_reg_mr(pd, addr, len, permissions);
    if (!mr)
        die("could not register mr");

    registered_mrs.push_back(mr);
    return mr;
}

inline void RDMAPeer::post_recv(const RDMAContext &ctx, void *laddr,
                                uint32_t len, uint32_t lkey) const
{
    int err = 0;
    ibv_sge sge = {
        .addr = (uintptr_t) laddr,
        .length = len,
        .lkey = lkey
    };

    ibv_recv_wr wr = {};
    ibv_recv_wr *bad_wr = nullptr;

    wr.next = nullptr;
    wr.sg_list = &sge;
    wr.num_sge = 1;

    if ((err = ibv_post_recv(ctx.qp, &wr, &bad_wr)) != 0)
        DIE("post_recv() returned " << err);
}

inline void RDMAPeer::post_batched_recv(RDMAContext &ctx, ibv_mr *mr,
                                    uint32_t startidx, uint32_t per_buf_bytes,
                                    uint32_t num_bufs) const
{
    uint32_t max_batch_size = RDMAContext::MAX_BATCHED_RECVS;
    uint32_t num_batches = num_bufs / max_batch_size;
    uint32_t batch_size = 0;
    uintptr_t base_addr = (uintptr_t) mr->addr + (startidx * per_buf_bytes);

    if (num_bufs % max_batch_size != 0)
        num_batches++;

    for (auto batch = 0u; batch < num_batches; ++batch) {
        if (num_bufs > (batch + 1) * max_batch_size)
            batch_size = max_batch_size;
        else
            batch_size = num_bufs - (batch * max_batch_size);

        ctx.post_batched_recv(base_addr + (batch * max_batch_size * per_buf_bytes),
                              per_buf_bytes, batch_size, mr->lkey);
    }
}

inline void RDMAPeer::post_send(const RDMAContext &ctx, void *laddr, uint32_t len,
                                uint32_t lkey) const
{
    ibv_wr_start(ctx.qpx);
    ctx.qpx->wr_flags = IBV_SEND_SIGNALED;
    ibv_wr_send(ctx.qpx);
    ibv_wr_set_sge(ctx.qpx, lkey, (uintptr_t) laddr, len);
    TEST_NZ(ibv_wr_complete(ctx.qpx));
}

/* returns whether the current posted send was signaled.
   the caller must make sure that we only attempt polling the
   signaled send after it has been posted */
inline bool RDMAPeer::post_2s_send_unsig(const RDMAContext &ctx, void *laddr, uint32_t len,
                                         uint32_t lkey)
{
    bool signaled = false;
    int ret;

    if (this->unsignaled_sends + 1 == MAX_UNSIGNALED_SENDS)
        signaled = true;

    ibv_wr_start(ctx.qpx);

    if (signaled)
        ctx.qpx->wr_flags = IBV_SEND_SIGNALED;
    else
        ctx.qpx->wr_flags = 0;

    ibv_wr_send(ctx.qpx);
    ibv_wr_set_sge(ctx.qpx, lkey, (uintptr_t) laddr, len);

    if ((ret = ibv_wr_complete(ctx.qpx)) != 0)
        DIE("ibv_wr_complete failed=" << ret << "\n");

    inc_with_wraparound(this->unsignaled_sends, MAX_UNSIGNALED_SENDS);
    return signaled;
}

inline ibv_cq_ex *RDMAPeer::get_send_cq()
{
    return send_cq.cqx;
}

inline ibv_cq_ex *RDMAPeer::get_recv_cq()
{
    return recv_cq.cqx;
}

inline CompQueue &RDMAPeer::get_send_compqueue()
{
    return send_cq;
}

inline CompQueue &RDMAPeer::get_recv_compqueue()
{
    return recv_cq;
}

inline std::vector<RDMAContext> &RDMAPeer::get_contexts()
{
    return contexts;
}

/* used for send/recvs. we could have an independent qp only for these ops
   but probably not worth the code */
inline RDMAContext &RDMAPeer::get_ctrl_ctx()
{
    return contexts[0];
}

inline unsigned int RDMAPeer::get_num_qps()
{
    return num_qps;
}

template<typename T>
inline unsigned int RDMAPeer::poll_atmost(unsigned int max, ibv_cq_ex *cq, T&& comp_func)
{
    int ret;
    unsigned int polled = 0;
    struct ibv_poll_cq_attr cq_attr = {};

    assert(max > 0);

    while ((ret = ibv_start_poll(cq, &cq_attr)) != 0) {
        if (ret == ENOENT)
            return 0;
        else
            DIE("ibv_start_poll() returned " << ret);
    }

    do {
        if (polled > 0) {
            while ((ret = ibv_next_poll(cq)) != 0) {
                if (ret == ENOENT)
                    goto end_poll;
                else
                    DIE("ibv_next_poll() returned " << ret);
            }
        }

        if (cq->status != IBV_WC_SUCCESS)
            DIE("cqe->status=" << cq->status);

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

template<typename T>
inline unsigned int RDMAPeer::poll_batched_atmost(unsigned int max, CompQueue &comp_queue, T&& comp_func)
{
    int ret;
    unsigned int polled = 0;

    assert(max > 0);

    if (!comp_queue.poll_started) {
        ret = comp_queue.start_poll();
        if (ret == ENOENT)  // no comp available
            goto end_poll;
        else                // comp available, read it
            goto read;
    }

    do {
        ret = ibv_next_poll(comp_queue.cqx);
        if (ret == ENOENT)
            goto end_poll;
        else if (ret != 0)
            DIE("ibv_next_poll() returned " << ret);

read:
        if (comp_queue.cqx->status != IBV_WC_SUCCESS)
            DIE("cqe->status=" << comp_queue.cqx->status);

        /* the post-completion function takes wr_id,
         * for reads, this is the ctx_id,
         * for sends, this is the batch size */
        comp_func(comp_queue.cqx->wr_id);

        polled++;
    } while (polled < max);

end_poll:
    comp_queue.num_cqes_polled += polled;
    comp_queue.maybe_end_poll();
    assert(polled <= max);
    return polled;
}

inline void RDMAPeer::poll_exactly(unsigned int target, ibv_cq_ex *cq)
{
    int ret;
    unsigned int polled = 0;
    struct ibv_poll_cq_attr cq_attr = {};

    while ((ret = ibv_start_poll(cq, &cq_attr)) != 0) {
        if (ret == ENOENT)
            continue;
        else
            DIE("ibv_start_poll() returned " << ret);
    }

    do {
        if (polled > 0) {
            while ((ret = ibv_next_poll(cq)) != 0) {
                if (ret == ENOENT)
                    continue;
                else
                    DIE("ibv_next_poll() returned " << ret);
            }
        }

        if (cq->status != IBV_WC_SUCCESS)
            DIE("cqe->status=" << cq->status);

        polled++;
    } while (polled < target);

    ibv_end_poll(cq);
}

template<typename T>
inline unsigned int RDMAPeer::poll_atleast(unsigned int target, ibv_cq_ex *cq,
                                            T&& comp_func)
{
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
    if (cq->status != IBV_WC_SUCCESS)
        die("cq status is not success\n");

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
            goto out;       /* reached target, we can leave */
    } else {
        die("error in ibv_next_poll()\n");
    }

out:
    ibv_end_poll(cq);
    return polled;
}

inline void RDMAPeer::start_batched_ops(RDMAContext *ctx)
{
    batch_ctx = ctx;
    ctx->curr_batch_size = 0;
    ibv_wr_start(batch_ctx->qpx);
}

/* end batched ops (reads/writes/sends) */
inline void RDMAPeer::end_batched_ops()
{
    /* if we are in the middle of a batched send, post the previously buffered send */
    if (batch_ctx->curr_batch_size > 0)
        batch_ctx->end_batched_send();

    TEST_NZ(ibv_wr_complete(batch_ctx->qpx));
    batch_ctx = nullptr;
}

#endif
