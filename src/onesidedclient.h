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
  ibv_mr server_mr[NUM_REG_RMC];  // servers's memory regions
  ibv_mr *ctrlreq_mr;             // to recv Ctrl requests
  ibv_mr *rdma_mr;                // for 1:1 mapping of host's rdma buffer
  std::unique_ptr<CtrlReq> ctrlreq_buf;
  char *rdma_buffer;
  HugeAllocator huge;

  void disconnect();  // TODO: do we need this?
  void recv_ctrl_reqs();

 public:
  OneSidedClient(uint16_t num_qps, uint16_t num_cqs)
      : rclient(num_qps, num_cqs, true), onesready(false) {
    rdma_buffer = huge.get();
    ctrlreq_buf = std::make_unique<CtrlReq>();
  }

  ~OneSidedClient() {}

  void connect(const std::string &ip, const unsigned int &port);
  void read_async(uintptr_t raddr, uintptr_t laddr, uint32_t size);
  void write_async(uint64_t raddr, uintptr_t laddr, uint32_t size);
  void cmp_swp_async(uintptr_t raddr, uintptr_t laddr, uint64_t cmp,
                     uint64_t swp);
  template <typename T>
  int poll_reads_atmost(int max, T &&comp_func);
  uintptr_t get_rsvd_base_raddr();
  uintptr_t get_rsvd_base_laddr();
  uintptr_t get_apps_base_raddr();
  uintptr_t get_apps_base_laddr();

  RDMAClient &get_rclient();
};

inline void OneSidedClient::recv_ctrl_reqs() {
  assert(onesready);

  // initial communication  with nicserver is done on qp=0 and cq=0 (only one)
  rclient.post_recv(rclient.get_ctrl_ctx(), ctrlreq_buf.get(), sizeof(CtrlReq),
                    ctrlreq_mr->lkey);
  rclient.poll_exactly(1, rclient.get_recv_cq(0));

  assert(ctrlreq_buf->type == CtrlCmdType::RDMA_MR);
  MrReq *mr_req = &(ctrlreq_buf->data.mr);
  printf("received RDMA_MR; num_mr=%u\n", mr_req->num_mr);
  memcpy(&server_mr[0], &mr_req->mrs[0], sizeof(ibv_mr) * mr_req->num_mr);
}

/* assumes the mapping from host memory to nic memory is 1:1; i.e.
   regions are the same size.
   so the offsets are taken the same way remotely and locally */
inline void OneSidedClient::read_async(uintptr_t remote_addr,
                                       uintptr_t local_addr, uint32_t size) {
  assert(onesready);
  RDMAContext *ctx = rclient.get_batch_ctx();
  assert(ctx != nullptr);

  ctx->post_batched_onesided(remote_addr, local_addr, size, server_mr[0].rkey,
                             rdma_mr->lkey, OpType::READ, 0, 0);
}

inline void OneSidedClient::write_async(uintptr_t remote_addr,
                                        uintptr_t local_addr, uint32_t size) {
  assert(onesready);
  RDMAContext *ctx = rclient.get_batch_ctx();
  assert(ctx != nullptr);

  ctx->post_batched_onesided(remote_addr, local_addr, size, server_mr[0].rkey,
                             rdma_mr->lkey, OpType::WRITE, 0, 0);
}

inline void OneSidedClient::cmp_swp_async(uintptr_t raddr, uintptr_t laddr,
                                          uint64_t cmp, uint64_t swp) {
  assert(onesready);
  RDMAContext *ctx = rclient.get_batch_ctx();
  assert(ctx != nullptr);

  ctx->post_batched_onesided(raddr, laddr, 8, server_mr[0].rkey, rdma_mr->lkey,
                             OpType::CMP_SWP, cmp, swp);
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

inline uintptr_t OneSidedClient::get_rsvd_base_laddr() {
  return reinterpret_cast<uintptr_t>(rdma_buffer);
}

inline uintptr_t OneSidedClient::get_apps_base_raddr() {
  return get_rsvd_base_raddr() + RMCK_RESERVED_BUFF_SZ;
}

inline uintptr_t OneSidedClient::get_apps_base_laddr() {
  return get_rsvd_base_laddr() + RMCK_RESERVED_BUFF_SZ;
}

inline RDMAClient &OneSidedClient::get_rclient() { return rclient; }

#endif
