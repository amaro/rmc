#ifndef ONE_SIDED_CLIENT_H
#define ONE_SIDED_CLIENT_H

#include <coroutine>

#include "allocator.h"
#include "rdma/rdmaclient.h"
#include "rdma/rdmapeer.h"
#include "rpc.h"

class OneSidedClient {
  using OpType = RDMAContext::OneSidedOp::OpType;

  RDMAClient rclient;

  bool onesready;
  /* server's memory regions
     TODO: need to support NUM_REG_RMC rdma_mrs, currently we only support 1*/
  ibv_mr server_mr[NUM_REG_RMC];
  ibv_mr *ctrlreq_mr = nullptr;  // to recv Ctrl requests
  std::unique_ptr<CtrlReq> ctrlreq_buf;

  void disconnect();  // TODO: do we need this?
  void recv_ctrl_reqs();

 public:
  OneSidedClient(uint16_t num_qps, uint16_t num_cqs)
      : rclient(num_qps, num_cqs, true), onesready(false) {
    ctrlreq_buf = std::make_unique<CtrlReq>();
  }

  void connect(const std::string &ip, const unsigned int &port);
  void post_op(RDMAContext::OneSidedOp op);
  void write_async(uint64_t raddr, uintptr_t laddr, uint32_t sz, uint32_t lkey,
                   uint32_t rkey);
  void cmp_swp_async(uintptr_t raddr, uintptr_t laddr, uint64_t cmp,
                     uint64_t swp, uint32_t lkey, uint32_t rkey);
  template <typename T>
  int poll_reads_atmost(int max, T &&comp_func);
  uintptr_t get_rsvd_base_raddr();
  uintptr_t get_rsvd_base_laddr();
  uintptr_t get_apps_base_raddr();
  uintptr_t get_apps_base_laddr();

  RDMAClient &get_rclient() { return rclient; }

  const ibv_mr &get_server_mr() { return server_mr[0]; }
};

/* control path */
inline void OneSidedClient::recv_ctrl_reqs() {
  assert(onesready);

  // initial communication  with nicserver is done on qp=0 and cq=0 (only one)
  rclient.post_recv(rclient.get_ctrl_ctx(), ctrlreq_buf.get(), sizeof(CtrlReq),
                    ctrlreq_mr->lkey);
  rclient.poll_exactly(1, rclient.get_recv_cq(0));

  /* receive memory regions from server */
  assert(ctrlreq_buf->type == CtrlCmdType::RDMA_MR);
  MrReq *mr_req = &(ctrlreq_buf->data.mr);
  printf("received RDMA_MR; num_mr=%u\n", mr_req->num_mr);
  memcpy(&server_mr[0], &mr_req->mrs[0], sizeof(ibv_mr) * mr_req->num_mr);
}

inline void OneSidedClient::post_op(RDMAContext::OneSidedOp op) {
  assert(onesready);
  RDMAContext *ctx = rclient.get_batch_ctx();
  assert(ctx != nullptr);

  ctx->post_batched_onesided(op);
}

template <typename T>
inline int OneSidedClient::poll_reads_atmost(int max, T &&comp_func) {
  assert(onesready);

  return rclient.poll_batched_atmost(
      max, rclient.get_send_compqueue(current_tid), comp_func);
}

inline uintptr_t OneSidedClient::get_rsvd_base_raddr() {
  return reinterpret_cast<uintptr_t>(server_mr[0].addr);
}

inline uintptr_t OneSidedClient::get_apps_base_raddr() {
  return get_rsvd_base_raddr() + RMCK_RESERVED_BUFF_SZ;
}

#endif
