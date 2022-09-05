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
  std::array<MemoryRegion, NUM_REG_RMC> server_mrs;

  /* to receive Ctrl requests */
  ibv_mr *ctrlreq_mr = nullptr;
  /* to make frames accessible to RNIC */
  ibv_mr *frames_mr = nullptr;
  std::unique_ptr<CtrlReq> ctrlreq_buf;

  void disconnect();  // TODO: do we need this?
  void recv_ctrl_reqs();

 public:
  OneSidedClient(uint16_t num_qps, uint16_t num_cqs)
      : rclient(num_qps, num_cqs, true), onesready(false) {
    ctrlreq_buf = std::make_unique<CtrlReq>();
  }

  void connect(const std::string &ip, const unsigned int &port);

  void post_op_from_frame(RDMAContext::OneSidedOp op) {
    assert(onesready);
    RDMAContext *ctx = rclient.get_batch_ctx();
    assert(ctx != nullptr);

    op.lkey = frames_mr->lkey;
    ctx->post_batched_onesided(op);
  }

  uint16_t free_slots() {
    RDMAContext *ctx = rclient.get_batch_ctx();
    assert(ctx != nullptr);
    return ctx->free_onesided_slots();
  }

  template <typename T>
  int poll_reads_atmost(int max, T &&comp_func);
  uintptr_t get_rsvd_base_raddr();
  uintptr_t get_rsvd_base_laddr();
  uintptr_t get_apps_base_raddr();
  uintptr_t get_apps_base_laddr();

  RDMAClient &get_rclient() { return rclient; }

  const MemoryRegion &get_server_mr() { return server_mrs[0]; }
};

/* control path */
inline void OneSidedClient::recv_ctrl_reqs() {
  assert(onesready);

  // initial communication  with nicserver is done on qp=0 and cq=0 (only one)
  rclient.post_recv(rclient.get_ctrl_ctx(), ctrlreq_buf.get(), sizeof(CtrlReq),
                    ctrlreq_mr->lkey);
  rclient.poll_exactly(1, rclient.get_recv_cq(0));

  /* receive ibv_mrs from server, and use them to create MemoryRegions */
  assert(ctrlreq_buf->type == CtrlCmdType::RDMA_MR);
  MrReq *mr_req = &(ctrlreq_buf->data.mr);
  printf("OneSidedClient: received RDMA_MR; num_mr=%u\n", mr_req->num_mr);
  assert(mr_req->num_mr == NUM_REG_RMC);
  for (auto i = 0U; i < mr_req->num_mr; ++i) {
    server_mrs[i].type = MemoryRegion::Type::RDMA;
    memcpy(&server_mrs[i].rdma, &mr_req->mrs[i], sizeof(ibv_mr));
    server_mrs[i].addr = server_mrs[i].rdma.addr;
    server_mrs[i].length = server_mrs[i].rdma.length;
  }

  /* register frame allocator with RDMA (could be done elsewhere) */
  void *frames = get_frame_alloc().get_huge_buffer().get();
  frames_mr =
      rclient.register_mr(frames, RMCK_TOTAL_BUFF_SZ,
                          IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_RELAXED_ORDERING);

  printf("OneSidedClient: frames addr=%p length=%ld\n", frames_mr->addr,
         frames_mr->length);
}

template <typename T>
inline int OneSidedClient::poll_reads_atmost(int max, T &&comp_func) {
  assert(onesready);

  return rclient.poll_batched_atmost(
      max, rclient.get_send_compqueue(current_tid), comp_func);
}

inline uintptr_t OneSidedClient::get_rsvd_base_raddr() {
  return reinterpret_cast<uintptr_t>(server_mrs[0].addr);
}

inline uintptr_t OneSidedClient::get_apps_base_raddr() {
  return get_rsvd_base_raddr() + RMCK_RESERVED_BUFF_SZ;
}

#endif
