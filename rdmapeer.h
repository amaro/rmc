#ifndef RDMA_PEER_H
#define RDMA_PEER_H

#include <iostream>
#include <memory>
#include <chrono>
#include <list>
#include <cassert>

#include <netdb.h>
#include <rdma/rdma_cma.h>

typedef std::chrono::time_point<std::chrono::steady_clock> time_point;

inline void die(const std::string& msg)
{
    std::cerr << msg << std::endl;
    exit(1);
}

#define TEST_NZ(x) do { if ( (x)) die("error: " #x " failed (returned non-zero)." ); } while (0)
#define TEST_Z(x)  do { if (!(x)) die("error: " #x " failed (returned zero/null)."); } while (0)

/* TODO: update this */
class RDMABatchOps {
private:
    const ibv_mr &remote_mr;
    const ibv_mr &local_mr;

    size_t op_size;
    size_t batch_size;
    std::unique_ptr<ibv_send_wr []> wrs;
    std::unique_ptr<ibv_sge []> sges;
    bool ready_for_post;

public:
    ibv_wr_opcode opcode;

    RDMABatchOps(const ibv_mr &rmr, const ibv_mr &lmr, ibv_wr_opcode op,
                 size_t op_size, size_t batch_size):
        remote_mr(rmr), local_mr(lmr), op_size(op_size), batch_size(batch_size),
        ready_for_post(false), opcode(op)
    {
        wrs = std::make_unique<ibv_send_wr[]>(batch_size);
        sges = std::make_unique<ibv_sge[]>(batch_size);

        /* accesses must be aligned to bufsize */
        assert(local_mr.length == remote_mr.length);
        assert(local_mr.length % this->op_size == 0);
    }

    /* only sets signaled to last element of batch.
       wraps around accesses if buffer runs out of space.
       returns new local_addr.
    */
    uint64_t build_seq_accesses(uint64_t start_offset);

    ibv_send_wr *get_wr_list()
    {
        assert(ready_for_post);
        this->ready_for_post = false;
        return &this->wrs[0];
    }
};

class RDMAPeer {
protected:
    const int CQ_NUM_CQE = 16;
    const int RDMA_BUFF_SIZE = 4096 * 32;
    const int TIMEOUT_MS = 500;
    const int QP_ATTRS_MAX_OUTSTAND_SEND_WRS = 32;
    const int QP_ATTRS_MAX_OUTSTAND_RECV_WRS = 32;
    const int QP_ATTRS_MAX_SGE_ELEMS = 1;
    const int QP_ATTRS_MAX_INLINE_DATA = 1;

    rdma_cm_id *id;
    ibv_qp *qp;
    ibv_qp_ex *qpx;
    ibv_context *dev_ctx;
    ibv_pd *pd;
    ibv_cq_ex *cqx;
    rdma_event_channel *event_channel;

    bool connected;

    std::list<ibv_mr *> registered_mrs;

    void create_context(ibv_context *verbs);
    void create_qps();
    void connect_or_accept(bool connect);
    void dereg_mrs();

    void handle_conn_established(rdma_cm_id *cm_id)
    {
        assert(!connected);
        std::cout << "connection established\n";
        connected = true;
    }

public:
    RDMAPeer() : connected(false) { }
    virtual ~RDMAPeer() { }

    ibv_mr *register_mr(void *addr, size_t len, int permissions);

    void post_recv(void *laddr, uint32_t len, uint32_t lkey) const;
    void post_send(void *laddr, uint32_t len, uint32_t lkey) const;
    void post_rdma_ops(RDMABatchOps &batchops, time_point &start) const;

    template<typename T> void blocking_poll_one(T&& func) const;
    void blocking_poll_nofunc(unsigned int times) const;

    virtual void disconnect();
};

inline ibv_mr *RDMAPeer::register_mr(void *addr, size_t len, int permissions)
{
    ibv_mr *mr = ibv_reg_mr(pd, addr, len, permissions);
    if (!mr)
        die("could not register mr");

    registered_mrs.push_back(mr);
    return mr;
}

inline void RDMAPeer::dereg_mrs()
{
    assert(connected);
    assert(!registered_mrs.empty());

    std::cout << "dereg_mrs()\n";
    for (ibv_mr *curr_mr: registered_mrs)
        ibv_dereg_mr(curr_mr);

    registered_mrs.clear();
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

    std::cout << "post_recv on qp=" << &qp << "\n";

    wr.next = nullptr;
    wr.sg_list = &sge;
    wr.num_sge = 1;

    TEST_NZ(ibv_post_recv(this->qp, &wr, &bad_wr));
}

inline void RDMAPeer::post_send(void *laddr, uint32_t len, uint32_t lkey) const
{
    std::cout << "post_send on qpx=" << &qpx << "\n";
    ibv_wr_start(qpx);
    qpx->wr_flags = IBV_SEND_SIGNALED;
    ibv_wr_send(qpx);
    ibv_wr_set_sge(qpx, lkey, (uintptr_t) laddr, len);
    TEST_NZ(ibv_wr_complete(qpx));
}

/* data path */
template<typename T>
inline void RDMAPeer::blocking_poll_one(T&& func) const
{
    int ret;
    struct ibv_poll_cq_attr cq_attr = {};

    while ((ret = ibv_start_poll(cqx, &cq_attr)) != 0) {
        if (ret == ENOENT)
            continue;
        else
            die("error in ibv_start_poll()\n");
    }

    if (cqx->status != IBV_WC_SUCCESS)
        die("cqe status is not success\n");

    func();
    ibv_end_poll(cqx);
}

inline void RDMAPeer::blocking_poll_nofunc(unsigned int times) const
{
    auto nofunc = []() -> void {};
    for (unsigned int i = 0; i < times; ++i)
        blocking_poll_one(nofunc);
}
#endif
