#include "rdmapeer.h"

void RDMAPeer::create_context(ibv_context *verbs)
{
    ibv_cq_init_attr_ex cq_attrs_ex = {};
    cq_attrs_ex.cqe = CQ_NUM_CQE;
    cq_attrs_ex.comp_vector = 0;
    cq_attrs_ex.wc_flags = IBV_WC_EX_WITH_BYTE_LEN;
    cq_attrs_ex.flags = IBV_CREATE_CQ_ATTR_SINGLE_THREADED;

    dev_ctx = verbs;
    TEST_Z(pd = ibv_alloc_pd(dev_ctx));
    TEST_Z(cqx = ibv_create_cq_ex(dev_ctx, &cq_attrs_ex));
}

/* this->id must be the connected cm_id */
void RDMAPeer::create_qps()
{
    ibv_qp_init_attr_ex qp_attrs = {};

    qp_attrs.send_cq = ibv_cq_ex_to_cq(cqx);
    qp_attrs.recv_cq = ibv_cq_ex_to_cq(cqx);
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



void RDMAPeer::post_simple_recv(ibv_sge *sge) const
{
    ibv_recv_wr wr = {};
    ibv_recv_wr *bad_wr = nullptr;

    wr.next = nullptr;
    wr.sg_list = sge;
    wr.num_sge = 1;

    TEST_NZ(ibv_post_recv(this->qp, &wr, &bad_wr));
}

void RDMAPeer::post_simple_send(ibv_sge *sge) const
{
    ibv_wr_start(qpx);
    qpx->wr_flags = IBV_SEND_SIGNALED;
    ibv_wr_send(qpx);
    ibv_wr_set_sge(qpx, sge->lkey, sge->addr, sge->length);
    TEST_NZ(ibv_wr_complete(qpx));
}

void RDMAPeer::post_rdma_ops(RDMABatchOps &batchops, time_point &start) const
{
    ibv_send_wr *wr = batchops.get_wr_list();

    ibv_wr_start(this->qpx);
    while (wr) {
        //qpx->wr_id = wr->wr_id;
        qpx->wr_flags = wr->send_flags;

        switch (batchops.opcode) {
        case IBV_WR_RDMA_WRITE:
            ibv_wr_rdma_write(this->qpx, wr->wr.rdma.rkey, wr->wr.rdma.remote_addr);
            break;
        case IBV_WR_RDMA_READ:
            ibv_wr_rdma_read(this->qpx, wr->wr.rdma.rkey, wr->wr.rdma.remote_addr);
            break;
        default:
            die("unrecognized opcode\n");
        }

        ibv_wr_set_sge(qpx, wr->sg_list->lkey, wr->sg_list->addr, wr->sg_list->length);
        wr = wr->next;
    }

    start = std::chrono::steady_clock::now();
    TEST_NZ(ibv_wr_complete(this->qpx));
}

void RDMAPeer::disconnect()
{
    assert(connected);
    connected = false;

    rdma_destroy_qp(id);

    ibv_destroy_cq(ibv_cq_ex_to_cq(cqx));
    ibv_dealloc_pd(pd);

    rdma_destroy_id(id);
    rdma_destroy_event_channel(event_channel);
}

uint64_t RDMABatchOps::build_seq_accesses(uint64_t start_offset)
{
    assert(!ready_for_post);
    size_t offset = start_offset;

    for (unsigned opidx = 0; opidx < this->batch_size; ++opidx) {
        ibv_send_wr *wr = &wrs[opidx];
        ibv_sge *sge = &sges[opidx];

        wr->wr_id = opidx;
        wr->opcode = this->opcode;
        wr->sg_list = sge;
        wr->num_sge = 1;

        /* remote addr/keys */
        wr->wr.rdma.remote_addr = (uint64_t) remote_mr.addr + offset;
        wr->wr.rdma.rkey = remote_mr.rkey;

        /* local addr/keys */
        sge->addr = (uint64_t) local_mr.addr + offset;
        sge->length = this->op_size;
        sge->lkey = local_mr.lkey;

        /* if this is the last op in batch, set signaled and no next wr */
        if (opidx + 1 == this->batch_size) {
            wr->send_flags = IBV_SEND_SIGNALED;
            wr->next = nullptr;
        } else {
            wr->send_flags = 0;
            wr->next = &wrs[opidx + 1];
        }

        offset += this->op_size;

        /* if we moved past the buffer size, wrap around
            op_size is aligned w.r.t. buffer length, so this should be enough */
        if (offset >= local_mr.length)
            offset = 0;
    }

    this->ready_for_post = true;
    return offset;
}
