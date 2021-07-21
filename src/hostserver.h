#ifndef HOST_SERVER_H
#define HOST_SERVER_H

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
  /* initialized in init_rdma_buffer */
  LLNode *linkedlist;

  void send_rdma_mr();

public:
  /* TODO: move these to a config.h or something */
  const static long RDMA_BUFF_SIZE = 1 << 26;
  const static int PAGE_SIZE = 4096;

  HostServer(unsigned int num_qps) : rserver(num_qps, true), hsready(false) {
    init_rdma_buffer();
    req_buf = std::make_unique<CmdRequest>();
  }

  ~HostServer() {
    /* see
     * https://stackoverflow.com/questions/8918791/how-to-properly-free-the-memory-allocated-by-placement-new
     */
    linkedlist->~LLNode();
    free(rdma_buffer);
  }

  void connect_and_block(int port);
  void disconnect();
  void init_rdma_buffer();
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
