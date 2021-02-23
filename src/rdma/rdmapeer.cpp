#include "rdmapeer.h"

void RDMAPeer::create_context(ibv_context *verbs)
{
    ibv_cq_init_attr_ex cq_attrs_ex = {};
    cq_attrs_ex.cqe = CQ_NUM_CQE;
    cq_attrs_ex.comp_vector = 0;
    //cq_attrs_ex.wc_flags = IBV_WC_EX_WITH_BYTE_LEN;
    cq_attrs_ex.comp_mask = IBV_CQ_INIT_ATTR_MASK_FLAGS;
    cq_attrs_ex.flags = IBV_CREATE_CQ_ATTR_SINGLE_THREADED;

    dev_ctx = verbs;
    TEST_Z(pd = ibv_alloc_pd(dev_ctx));
    TEST_Z(send_cqx = mlx5dv_create_cq(dev_ctx, &cq_attrs_ex, NULL));
    TEST_Z(recv_cqx = mlx5dv_create_cq(dev_ctx, &cq_attrs_ex, NULL));
}

/* this->id must be the connected cm_id */
void RDMAPeer::create_qps()
{
    ibv_qp_init_attr_ex qp_attrs = {};

    qp_attrs.send_cq = ibv_cq_ex_to_cq(send_cqx);
    qp_attrs.recv_cq = ibv_cq_ex_to_cq(recv_cqx);
    qp_attrs.qp_type = IBV_QPT_RC;
    qp_attrs.sq_sig_all = 0;
    qp_attrs.pd = this->pd;
    /* identified valid fields? */
    qp_attrs.comp_mask |= IBV_QP_INIT_ATTR_SEND_OPS_FLAGS | IBV_QP_INIT_ATTR_PD;

    qp_attrs.cap.max_send_wr = QP_ATTRS_MAX_OUTSTAND_SEND_WRS;
    qp_attrs.cap.max_recv_wr = QP_ATTRS_MAX_OUTSTAND_RECV_WRS;
    qp_attrs.cap.max_send_sge = QP_ATTRS_MAX_SGE_ELEMS;
    qp_attrs.cap.max_recv_sge = QP_ATTRS_MAX_SGE_ELEMS;
    qp_attrs.cap.max_inline_data = QP_ATTRS_MAX_INLINE_DATA;

    qp_attrs.send_ops_flags |= IBV_WR_SEND | IBV_QP_EX_WITH_RDMA_READ | IBV_QP_EX_WITH_RDMA_WRITE;

    TEST_Z(this->qp = mlx5dv_create_qp(this->id->verbs, &qp_attrs, NULL));
    this->id->qp = this->qp;
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
