#ifndef RDMA_PEER_H
#define RDMA_PEER_H

#include <iostream>
#include <memory>
#include <chrono>
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

struct Message {
    enum {
        MSG_MR,
        MSG_DONE
    } type;

    union {
        struct ibv_mr mr;
    } data;
};

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

    ibv_mr *rdma_buffer_mr;
    ibv_mr peer_mr;

    // physically resides in server but needs to be created and mapped
    // in client too
    std::unique_ptr<char[]> rdma_buffer;

    void create_context(ibv_context *verbs);
    void create_qps();
    void connect_or_accept(bool connect);

    void handle_conn_established(rdma_cm_id *cm_id)
    {
        assert(!connected);
        std::cout << "connection established\n";
        connected = true;
    }

public:
    RDMAPeer() : connected(false) { }
    virtual ~RDMAPeer() { }

    const ibv_mr &get_remote_mr() const
    {
        return this->peer_mr;
    }

    const ibv_mr &get_local_mr() const
    {
        return *this->rdma_buffer_mr;
    }

    void post_simple_recv(ibv_sge *sge) const;
    void post_simple_send(ibv_sge *sge) const;
    void post_rdma_ops(RDMABatchOps &batchops, time_point &start) const;

    /* data path, be judicious with code */
    template<typename T>
    void blocking_poll_one(T&& func) const
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

    virtual void disconnect();
};

#endif
