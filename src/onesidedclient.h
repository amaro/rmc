#ifndef ONE_SIDED_CLIENT_H
#define ONE_SIDED_CLIENT_H

#include <coroutine>

#include "allocator.h"
#include "rdma/rdmaclient.h"
#include "rdma/rdmapeer.h"
#include "rmc.h"

class OneSidedClient {
  using OpType = RDMAContext::OneSidedOp::OpType;

  RDMAClient rclient;

  bool onesready;
  ibv_mr host_mr;      // host's memory region; remote addr and rkey
  ibv_mr *req_buf_mr;  // to send Cmd requests
  ibv_mr *rdma_mr;     // for 1:1 mapping of host's rdma buffer
  std::unique_ptr<CmdRequest> req_buf;
  char *rdma_buffer;
  HugeAllocator huge;

  void disconnect();  // TODO: do we need this?
  void recv_rdma_mr();

 public:
  OneSidedClient(uint16_t num_qps, uint16_t num_cqs)
      : rclient(num_qps, num_cqs, true), onesready(false) {
    rdma_buffer = huge.get();
    req_buf = std::make_unique<CmdRequest>();
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

inline void OneSidedClient::recv_rdma_mr() {
  assert(onesready);

  // initial communication  with nicserver is done on qp=0 and cq=0 (only one)
  rclient.post_recv(rclient.get_ctrl_ctx(), req_buf.get(), sizeof(CmdRequest),
                    req_buf_mr->lkey);
  rclient.poll_exactly(1, rclient.get_recv_cq(0));

  assert(req_buf->type == SET_RDMA_MR);
  memcpy(&host_mr, &req_buf->request.rdma_mr.mr, sizeof(ibv_mr));
  LOG("received SET_RDMA_MR; rkey=" << host_mr.rkey);
}

/* assumes the mapping from host memory to nic memory is 1:1; i.e.
   regions are the same size.
   so the offsets are taken the same way remotely and locally */
inline void OneSidedClient::read_async(uintptr_t remote_addr,
                                       uintptr_t local_addr, uint32_t size) {
  assert(onesready);
  RDMAContext *ctx = rclient.get_batch_ctx();
  assert(ctx != nullptr);

  ctx->post_batched_onesided(remote_addr, local_addr, size, host_mr.rkey,
                             rdma_mr->lkey, OpType::READ, 0, 0);
}

inline void OneSidedClient::write_async(uintptr_t remote_addr,
                                        uintptr_t local_addr, uint32_t size) {
  assert(onesready);
  RDMAContext *ctx = rclient.get_batch_ctx();
  assert(ctx != nullptr);

  ctx->post_batched_onesided(remote_addr, local_addr, size, host_mr.rkey,
                             rdma_mr->lkey, OpType::WRITE, 0, 0);
}

inline void OneSidedClient::cmp_swp_async(uintptr_t raddr, uintptr_t laddr,
                                          uint64_t cmp, uint64_t swp) {
  assert(onesready);
  RDMAContext *ctx = rclient.get_batch_ctx();
  assert(ctx != nullptr);

  ctx->post_batched_onesided(raddr, laddr, 8, host_mr.rkey, rdma_mr->lkey,
                             OpType::CMP_SWP, cmp, swp);
}

template <typename T>
inline int OneSidedClient::poll_reads_atmost(int max, T &&comp_func) {
  assert(onesready);

  return rclient.poll_batched_atmost(
      max, rclient.get_send_compqueue(current_tid), comp_func);
}

inline uintptr_t OneSidedClient::get_rsvd_base_raddr() {
  return reinterpret_cast<uintptr_t>(host_mr.addr);
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
