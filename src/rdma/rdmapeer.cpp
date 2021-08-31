#include "rdmapeer.h"

void RDMAPeer::create_pds_cqs(ibv_context *verbs, bool onesided) {
  ibv_cq_init_attr_ex cq_attrs_ex = {};
  if (onesided)
    cq_attrs_ex.cqe = QP_MAX_1SIDED_WRS;
  else
    cq_attrs_ex.cqe = QP_MAX_2SIDED_WRS;
  cq_attrs_ex.comp_vector = 0;
  // cq_attrs_ex.wc_flags = IBV_WC_EX_WITH_BYTE_LEN;
  cq_attrs_ex.comp_mask = IBV_CQ_INIT_ATTR_MASK_FLAGS;
  cq_attrs_ex.flags = IBV_CREATE_CQ_ATTR_SINGLE_THREADED;

  dev_ctx = verbs; /* TODO: is this needed? */
  TEST_Z(pd = ibv_alloc_pd(dev_ctx));

  for (auto i = 0u; i < num_cqs; ++i) {
    TEST_Z(send_cqs[i].cqx = mlx5dv_create_cq(dev_ctx, &cq_attrs_ex, NULL));
    TEST_Z(recv_cqs[i].cqx = mlx5dv_create_cq(dev_ctx, &cq_attrs_ex, NULL));
  }

  pds_cqs_created = true;
  LOG("created pds and " << num_cqs << " cqs; onesided=" << onesided);
}

void RDMAPeer::destroy_pds_cqs() {
  assert(pds_cqs_created);
  pds_cqs_created = false;

  ibv_dealloc_pd(pd);
  for (auto i = 0u; i < num_cqs; ++i) {
    ibv_destroy_cq(ibv_cq_ex_to_cq(get_send_cq(i)));
    ibv_destroy_cq(ibv_cq_ex_to_cq(get_recv_cq(i)));
  }
}

void RDMAPeer::create_qps(RDMAContext &ctx, bool onesided) {
  ibv_qp_init_attr_ex qp_attrs = {};
  const uint16_t qps_per_cq = num_qps / num_cqs;
  const uint16_t cq_idx = num_created_qps / qps_per_cq;

  // cannot use current_tid here because OneSidedClient is shared among threads,
  // so current_tid will be 0 for all qps. so we keep track of how many qps
  // we have created so far, and assign cq_idxs evenly to them
  ibv_cq_ex *recv_cq = get_recv_cq(cq_idx);
  ibv_cq_ex *send_cq = get_send_cq(cq_idx);

  qp_attrs.send_cq = ibv_cq_ex_to_cq(send_cq);
  qp_attrs.recv_cq = ibv_cq_ex_to_cq(recv_cq);
  qp_attrs.qp_type = IBV_QPT_RC;
  qp_attrs.sq_sig_all = 0;
  qp_attrs.pd = this->pd;
  /* identified valid fields? */
  qp_attrs.comp_mask |= IBV_QP_INIT_ATTR_SEND_OPS_FLAGS | IBV_QP_INIT_ATTR_PD;

  if (onesided) {
    qp_attrs.cap.max_send_wr = QP_MAX_1SIDED_WRS;
    qp_attrs.cap.max_recv_wr = 1;
  } else {
    qp_attrs.cap.max_send_wr = QP_MAX_2SIDED_WRS;
    qp_attrs.cap.max_recv_wr = QP_MAX_2SIDED_WRS;
  }
  qp_attrs.cap.max_send_sge = QP_ATTRS_MAX_SGE_ELEMS;
  qp_attrs.cap.max_recv_sge = QP_ATTRS_MAX_SGE_ELEMS;
  qp_attrs.cap.max_inline_data = QP_ATTRS_MAX_INLINE_DATA;

  if (onesided)
    qp_attrs.send_ops_flags |=
        IBV_QP_EX_WITH_RDMA_READ | IBV_QP_EX_WITH_RDMA_WRITE;
  else
    qp_attrs.send_ops_flags |= IBV_WR_SEND;

  TEST_Z(ctx.qp = mlx5dv_create_qp(ctx.cm_id->verbs, &qp_attrs, NULL));
  ctx.cm_id->qp = ctx.qp;
  ctx.qpx = ibv_qp_to_qp_ex(ctx.qp);

  LOG("created onesided=" << onesided << " qp=" << ctx.qp
                          << " bound to send_cq=" << send_cq
                          << " and recv_cq=" << recv_cq);
  num_created_qps++;
}

void RDMAPeer::connect_or_accept(RDMAContext &ctx, bool connect) {
  ibv_device_attr attrs = {};
  rdma_conn_param cm_params = {};

  ibv_query_device(ctx.cm_id->verbs, &attrs);

  cm_params.responder_resources = attrs.max_qp_init_rd_atom;
  cm_params.initiator_depth = attrs.max_qp_rd_atom;
  cm_params.retry_count = 1;
  cm_params.rnr_retry_count = 1;

  if (connect)
    TEST_NZ(rdma_connect(ctx.cm_id, &cm_params));
  else
    TEST_NZ(rdma_accept(ctx.cm_id, &cm_params));
}

void RDMAPeer::dereg_mrs() {
  assert(!registered_mrs.empty());

  LOG("dereg_mrs()");
  for (ibv_mr *curr_mr : registered_mrs)
    ibv_dereg_mr(curr_mr);

  registered_mrs.clear();
}

void RDMAPeer::handle_conn_established(RDMAContext &ctx) {
  assert(!ctx.connected);
  LOG("connection established");
  ctx.connected = true;
}
