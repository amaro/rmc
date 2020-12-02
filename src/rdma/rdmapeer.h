#ifndef RDMA_PEER_H
#define RDMA_PEER_H

#include <list>
#include <cassert>

#include <netdb.h>
#include <rdma/rdma_cma.h>
#include "utils/utils.h"

class RDMAPeer {
protected:
    const int CQ_NUM_CQE = 16;
    const int TIMEOUT_MS = 5;
    const int QP_ATTRS_MAX_OUTSTAND_SEND_WRS = 16;
    const int QP_ATTRS_MAX_OUTSTAND_RECV_WRS = 1;
    const int QP_ATTRS_MAX_SGE_ELEMS = 1;
    const int QP_ATTRS_MAX_INLINE_DATA = 256;

    rdma_cm_id *id;
    ibv_qp *qp;
    ibv_qp_ex *qpx;
    ibv_context *dev_ctx;
    ibv_pd *pd;
    ibv_cq_ex *send_cqx;
    ibv_cq_ex *recv_cqx;
    rdma_event_channel *event_channel;

    bool connected;
    size_t unsignaled_sends;

    std::list<ibv_mr *> registered_mrs;

    void create_context(ibv_context *verbs);
    void create_qps();
    void connect_or_accept(bool connect);
    void dereg_mrs();
    void handle_conn_established(rdma_cm_id *cm_id);

public:
    static const size_t MAX_UNSIGNALED_SENDS = 4;

    RDMAPeer() : connected(false), unsignaled_sends(0) { }
    virtual ~RDMAPeer() { }

    ibv_mr *register_mr(void *addr, size_t len, int permissions);
    void post_recv(void *laddr, uint32_t len, uint32_t lkey) const;
    void post_send(void *laddr, uint32_t len, uint32_t lkey) const;
    /* posts an unsignaled send, and returns whether the send_cqx should be polled */
    bool post_send_unsignaled(void *laddr, uint32_t len, uint32_t lkey);
    void post_read(const ibv_mr &local_mr, const ibv_mr &remote_mr,
                    uint32_t offset, uint32_t len) const;
    template<typename T> void blocking_poll_one(T&& func, ibv_cq_ex *cq) const;
    void poll_atleast(unsigned int times, ibv_cq_ex *cq);
    void poll_exactly(unsigned int times, ibv_cq_ex *cq);
    virtual void disconnect();
    ibv_cq_ex *get_send_cq();
    ibv_cq_ex *get_recv_cq();
};

inline ibv_mr *RDMAPeer::register_mr(void *addr, size_t len, int permissions)
{
    ibv_mr *mr = ibv_reg_mr(pd, addr, len, permissions);
    if (!mr)
        die("could not register mr");

    registered_mrs.push_back(mr);
    return mr;
}

inline void RDMAPeer::post_recv(void *laddr, uint32_t len, uint32_t lkey) const
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

    TEST_NZ(ibv_post_recv(this->qp, &wr, &bad_wr));
}

inline void RDMAPeer::post_send(void *laddr, uint32_t len, uint32_t lkey) const
{
    ibv_wr_start(qpx);
    qpx->wr_flags = IBV_SEND_SIGNALED;
    ibv_wr_send(qpx);
    //ibv_wr_set_sge(qpx, lkey, (uintptr_t) laddr, len);
    ibv_wr_set_inline_data(qpx, laddr, len);
    TEST_NZ(ibv_wr_complete(qpx));
}

inline void RDMAPeer::dereg_mrs()
{
    assert(connected);
    assert(!registered_mrs.empty());

    LOG("dereg_mrs()");
    for (ibv_mr *curr_mr: registered_mrs)
        ibv_dereg_mr(curr_mr);

    registered_mrs.clear();
}

inline void RDMAPeer::handle_conn_established(rdma_cm_id *cm_id)
{
    assert(!connected);
    LOG("connection established");
    connected = true;
}

inline bool RDMAPeer::post_send_unsignaled(void *laddr, uint32_t len, uint32_t lkey)
{
    bool signaled = false;
    int ret;

    if (unsignaled_sends + 1 == MAX_UNSIGNALED_SENDS)
        signaled = true;

    ibv_wr_start(qpx);

    if (signaled)
        qpx->wr_flags = IBV_SEND_SIGNALED;

    ibv_wr_send(qpx);
    ibv_wr_set_sge(qpx, lkey, (uintptr_t) laddr, len);

    if ((ret = ibv_wr_complete(qpx)) != 0)
        DIE("ibv_wr_complete returned=" << ret);

    /* if we posted a signaled send, reset unsignaled counter */
    if (signaled)
        unsignaled_sends = 0;
    else
        unsignaled_sends++;

    return signaled;
}

/* for now assumes the mapping from host memory to nic memory is 1:1; i.e.
   regions are the same size.
   so the offsets are taken the same way remotely and locally */
inline void RDMAPeer::post_read(const ibv_mr &local_mr, const ibv_mr &remote_mr,
                                uint32_t offset, uint32_t len) const
{
    uintptr_t raddr = reinterpret_cast<uintptr_t>(remote_mr.addr) + offset;
    uintptr_t laddr = reinterpret_cast<uintptr_t>(local_mr.addr) + offset;
    ibv_wr_start(qpx);
    qpx->wr_flags = IBV_SEND_SIGNALED;
    ibv_wr_rdma_read(qpx, remote_mr.rkey, raddr);
    ibv_wr_set_sge(qpx, local_mr.lkey, laddr, len);
    TEST_NZ(ibv_wr_complete(qpx));
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

#endif
