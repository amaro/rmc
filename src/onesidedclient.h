#ifndef ONE_SIDED_CLIENT_H
#define ONE_SIDED_CLIENT_H

#include "hostserver.h"
#include "rdma/rdmaclient.h"
#include "rmc.h"
#include <coroutine>

class OneSidedClient {
  RDMAClient rclient;

  bool onesready;
  ibv_mr host_mr;     // host's memory region; remote addr and rkey
  ibv_mr *req_buf_mr; // to send Cmd requests
  ibv_mr *rdma_mr;    // for 1:1 mapping of host's rdma buffer
  std::unique_ptr<CmdRequest> req_buf;
  char *rdma_buffer;
  HugeAllocator huge;

  void disconnect(); // TODO: do we need this?
  void recv_rdma_mr();

public:
  OneSidedClient(unsigned int num_qps)
      : rclient(num_qps, true), onesready(false) {
    rdma_buffer = huge.get();
    req_buf = std::make_unique<CmdRequest>();
  }

  ~OneSidedClient() {}

  void connect(const std::string &ip, const unsigned int &port);
  void read_async(uintptr_t raddr, uintptr_t laddr, uint32_t size);
  void write_async(uint64_t raddr, uintptr_t laddr, uint32_t size);
  template <typename T> int poll_reads_atmost(int max, T &&comp_func);
  char *get_rdma_buffer();
  uintptr_t get_remote_base_addr();
  uintptr_t get_local_base_addr();

  RDMAClient &get_rclient();
};

inline void OneSidedClient::recv_rdma_mr() {
  assert(onesready);

  rclient.post_recv(rclient.get_ctrl_ctx(), req_buf.get(), sizeof(CmdRequest),
                    req_buf_mr->lkey);
  rclient.poll_exactly(1, rclient.get_recv_cq());

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
  assert(rclient.batch_ctx != nullptr);

  RDMAContext *ctx = rclient.batch_ctx;
  ctx->post_batched_read(remote_addr, local_addr, size, host_mr.rkey,
                         rdma_mr->lkey);
}

inline void OneSidedClient::write_async(uintptr_t remote_addr,
                                        uintptr_t local_addr, uint32_t size) {
  assert(onesready);
  assert(rclient.batch_ctx != nullptr);

  RDMAContext *ctx = rclient.batch_ctx;
  ctx->post_batched_write(remote_addr, local_addr, size, host_mr.rkey,
                          rdma_mr->lkey);
}

template <typename T>
inline int OneSidedClient::poll_reads_atmost(int max, T &&comp_func) {
  assert(onesready);

  return rclient.poll_batched_atmost(max, rclient.get_send_compqueue(),
                                     comp_func);
}

inline char *OneSidedClient::get_rdma_buffer() { return rdma_buffer; }

inline uintptr_t OneSidedClient::get_remote_base_addr() {
  return reinterpret_cast<uintptr_t>(host_mr.addr);
}

inline uintptr_t OneSidedClient::get_local_base_addr() {
  return reinterpret_cast<uintptr_t>(rdma_mr->addr);
}

inline RDMAClient &OneSidedClient::get_rclient() { return rclient; }

#endif
