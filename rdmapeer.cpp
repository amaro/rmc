#include "rdmapeer.h"

void RDMAPeer::create_context(ibv_context *verbs)
{
    ibv_cq_init_attr_ex cq_attrs_ex = {};
    cq_attrs_ex.cqe = CQ_NUM_CQE;
    cq_attrs_ex.comp_vector = 0;
    cq_attrs_ex.wc_flags = IBV_WC_EX_WITH_BYTE_LEN;
    cq_attrs_ex.comp_mask = IBV_CQ_INIT_ATTR_MASK_FLAGS;
    cq_attrs_ex.flags = IBV_CREATE_CQ_ATTR_SINGLE_THREADED;

    dev_ctx = verbs;
    TEST_Z(pd = ibv_alloc_pd(dev_ctx));
    TEST_Z(send_cqx = ibv_create_cq_ex(dev_ctx, &cq_attrs_ex));
    TEST_Z(recv_cqx = ibv_create_cq_ex(dev_ctx, &cq_attrs_ex));
}

/* this->id must be the connected cm_id */
void RDMAPeer::create_qps()
{
    ibv_qp_init_attr_ex qp_attrs = {};

    qp_attrs.send_cq = ibv_cq_ex_to_cq(send_cqx);
    qp_attrs.recv_cq = ibv_cq_ex_to_cq(recv_cqx);
    qp_attrs.qp_type = IBV_QPT_RC;
    qp_attrs.pd = this->pd;
    /* identified valid fields? */
    qp_attrs.comp_mask |= IBV_QP_INIT_ATTR_SEND_OPS_FLAGS | IBV_QP_INIT_ATTR_PD;

    qp_attrs.cap.max_send_wr = QP_ATTRS_MAX_OUTSTAND_SEND_WRS;
    qp_attrs.cap.max_recv_wr = QP_ATTRS_MAX_OUTSTAND_RECV_WRS;
    qp_attrs.cap.max_send_sge = QP_ATTRS_MAX_SGE_ELEMS;
    qp_attrs.cap.max_recv_sge = QP_ATTRS_MAX_SGE_ELEMS;
    qp_attrs.cap.max_inline_data = QP_ATTRS_MAX_INLINE_DATA;

    qp_attrs.send_ops_flags |= IBV_WR_SEND | IBV_QP_EX_WITH_RDMA_READ | IBV_QP_EX_WITH_RDMA_WRITE;

    TEST_NZ(rdma_create_qp_ex(this->id, &qp_attrs));
    this->qp = this->id->qp;
    this->qpx = ibv_qp_to_qp_ex(this->qp);
}


void RDMAPeer::connect_or_accept(bool connect)
{
    ibv_device_attr attrs = {};
    rdma_conn_param cm_params = {};

    ibv_query_device(this->id->verbs, &attrs);

    cm_params.responder_resources = attrs.max_qp_init_rd_atom;
    cm_params.initiator_depth = attrs.max_qp_rd_atom;
    cm_params.retry_count = 1;
    cm_params.rnr_retry_count = 1;

    if (connect)
        TEST_NZ(rdma_connect(this->id, &cm_params));
    else
        TEST_NZ(rdma_accept(this->id, &cm_params));
}

void RDMAPeer::disconnect()
{
    assert(connected);
    connected = false;

    rdma_disconnect(id);
    rdma_destroy_qp(id);

    ibv_destroy_cq(ibv_cq_ex_to_cq(send_cqx));
    ibv_destroy_cq(ibv_cq_ex_to_cq(recv_cqx));
    ibv_dealloc_pd(pd);

    rdma_destroy_id(id);
}

void RDMAPeer::poll_atleast(unsigned int target, ibv_cq_ex *cq)
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
}

void RDMAPeer::poll_exactly(unsigned int target, ibv_cq_ex *cq)
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
