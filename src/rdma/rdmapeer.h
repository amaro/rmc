#ifndef RDMA_PEER_H
#define RDMA_PEER_H

#include <list>
#include <vector>
#include <cassert>

#include <netdb.h>
#include <rdma/rdma_cma.h>
#include <infiniband/verbs.h>
#include <infiniband/mlx5dv.h>
#include "utils/utils.h"

/* For now, RDMAContexts are used to implement support of multiple qps */
struct RDMAContext {
    bool connected = false;
    rdma_cm_id *id = nullptr;
    ibv_qp *qp = nullptr;
    ibv_qp_ex *qpx = nullptr;
    rdma_event_channel *event_channel = nullptr;

    void disconnect() {
        assert(ctx.connected);
        connected = false;

        ibv_destroy_qp(qp);
        rdma_destroy_id(id);
        rdma_destroy_event_channel(event_channel);
    }
};

class RDMAPeer {
protected:
    std::vector<RDMAContext> contexts;
    //ibv_qp *qp;
    //ibv_qp_ex *qpx;
    ibv_context *dev_ctx;
    ibv_pd *pd;
    ibv_cq_ex *send_cqx;
    ibv_cq_ex *recv_cqx;
    //rdma_event_channel *event_channel;

    //bool connected;
    bool pds_cqs_created;
    unsigned int unsignaled_sends;
    unsigned int num_qps;

    std::list<ibv_mr *> registered_mrs;

    void create_pds_cqs(ibv_context *verbs);
    void destroy_pds_cqs();
    void create_qps(RDMAContext &ctx);
    void connect_or_accept(RDMAContext &ctx, bool connect);
    void dereg_mrs();
    void handle_conn_established(RDMAContext &ctx);

public:
    static const int CQ_NUM_CQE = 64;
    static const int TIMEOUT_MS = 5;
    static const int QP_ATTRS_MAX_OUTSTAND_SEND_WRS = 64;
    static const int QP_ATTRS_MAX_OUTSTAND_RECV_WRS = QP_ATTRS_MAX_OUTSTAND_SEND_WRS;
    static const int QP_ATTRS_MAX_SGE_ELEMS = 1;
    static const int QP_ATTRS_MAX_INLINE_DATA = 256;
    static const int MAX_UNSIGNALED_SENDS = 64;
    static const int MAX_QP_INFLIGHT_READS = 16; // hw limited

    RDMAPeer(unsigned int num_qps) :
        pds_cqs_created(false), unsignaled_sends(0), num_qps(num_qps) { }

    virtual ~RDMAPeer() { }

    ibv_mr *register_mr(void *addr, size_t len, int permissions);
    void post_recv(const RDMAContext &ctx, void *laddr,
                    uint32_t len, uint32_t lkey) const;
    void post_send(const RDMAContext &ctx, void *laddr,
                    uint32_t len, uint32_t lkey) const;
    /* posts an unsignaled 2-sided send,
       returns whether send_cqx should be polled */
    bool post_2s_send_unsig(const RDMAContext &ctx, void *laddr,
                            uint32_t len, uint32_t lkey);
    template<typename T> void blocking_poll_one(T&& func, ibv_cq_ex *cq) const;
    unsigned int poll_atleast(unsigned int times, ibv_cq_ex *cq);
    void poll_exactly(unsigned int times, ibv_cq_ex *cq);
    unsigned int poll_atmost(unsigned int max, ibv_cq_ex *cq);
    ibv_cq_ex *get_send_cq();
    ibv_cq_ex *get_recv_cq();

    const RDMAContext &get_context(unsigned int idx);
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

    TEST_NZ(ibv_post_recv(ctx.qp, &wr, &bad_wr));
}

inline void RDMAPeer::post_send(const RDMAContext &ctx, void *laddr, uint32_t len,
                                uint32_t lkey) const
{
    ibv_wr_start(ctx.qpx);
    ctx.qpx->wr_flags = IBV_SEND_SIGNALED;
    ibv_wr_send(ctx.qpx);
    //ibv_wr_set_sge(qpx, lkey, (uintptr_t) laddr, len);
    ibv_wr_set_inline_data(ctx.qpx, laddr, len);
    TEST_NZ(ibv_wr_complete(ctx.qpx));
}

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

    if (len < QP_ATTRS_MAX_INLINE_DATA)
        ibv_wr_set_inline_data(ctx.qpx, laddr, len);
    else
        ibv_wr_set_sge(ctx.qpx, lkey, (uintptr_t) laddr, len);

    if ((ret = ibv_wr_complete(ctx.qpx)) != 0)
        DIE("ibv_wr_complete failed=" << ret << "\n");

    this->unsignaled_sends = (this->unsignaled_sends + 1) % MAX_UNSIGNALED_SENDS;
    return signaled;
}


/* data path */
template<typename T>
inline void RDMAPeer::blocking_poll_one(T&& func, ibv_cq_ex *cq) const
{
    int ret;
    struct ibv_poll_cq_attr cq_attr = {};

    while ((ret = ibv_start_poll(cq, &cq_attr)) != 0) {
        if (ret == ENOENT)
            continue;
        else
            die("error in ibv_start_poll()\n");
    }

    if (cq->status != IBV_WC_SUCCESS)
        LOG("cq->status =" << cq->status);

    func();
    ibv_end_poll(cq);
}

inline ibv_cq_ex *RDMAPeer::get_send_cq()
{
    return send_cqx;
}

inline ibv_cq_ex *RDMAPeer::get_recv_cq()
{
    return recv_cqx;
}

inline const RDMAContext &RDMAPeer::get_context(unsigned int idx)
{
    return contexts[idx];
}

inline unsigned int RDMAPeer::poll_atmost(unsigned int max, ibv_cq_ex *cq)
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

        polled++;
    } while (polled < max);

end_poll:
    ibv_end_poll(cq);
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

inline unsigned int RDMAPeer::poll_atleast(unsigned int target, ibv_cq_ex *cq)
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

#endif
