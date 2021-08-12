#ifndef HOST_SERVER_H
#define HOST_SERVER_H

#include "allocator.h"
#include "rdma/rdmaserver.h"
#include "rmc.h"
#include <cstdlib>
#include <cstring>
#include <memory>

class HostServer {
  RDMAServer rserver;
  bool hsready;
  char *rdma_buffer;
  // hostserver sends requests to nicserver
  std::unique_ptr<CmdRequest> req_buf;
  // no reply_buf since we don't need one right now.
  ibv_mr *rdma_mr;
  ibv_mr *req_buf_mr;
  LLNode *linkedlist;
  HugeAllocator huge;

  void send_rdma_mr();

public:
  /* TODO: move these to a config.h or something */
  static constexpr const long RDMA_BUFF_SIZE = 1 << 30;

  HostServer(unsigned int num_qps) : rserver(num_qps, true), hsready(false) {
    rdma_buffer = huge.get();
    linkedlist = create_linkedlist<LLNode>(rdma_buffer, RDMA_BUFF_SIZE);
    req_buf = std::make_unique<CmdRequest>();
  }

  ~HostServer() { destroy_linkedlist(linkedlist); }

  void connect_and_block(int port);
  void disconnect();
};

inline void HostServer::send_rdma_mr() {
  assert(hsready);

  req_buf->type = CmdType::SET_RDMA_MR;
  memcpy(&req_buf->request.rdma_mr.mr, rdma_mr, sizeof(ibv_mr));
  rserver.post_send(rserver.get_ctrl_ctx(), req_buf.get(), sizeof(CmdRequest),
                    req_buf_mr->lkey);
  rserver.poll_exactly(1, rserver.get_send_cq());
  LOG("sent SET_RDMA_MR");
}

#endif
