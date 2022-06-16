#ifndef HOST_SERVER_H
#define HOST_SERVER_H

#include <cstdlib>
#include <cstring>
#include <memory>

#include "allocator.h"
#include "rdma/rdmaserver.h"
#include "rmcs.h"

class HostServer {
  RDMAServer rserver;
  bool hsready;
  char *rdma_buffer;
  // hostserver sends requests to nicserver
  std::unique_ptr<CmdRequest> req_buf;
  // no reply_buf since we don't need one right now.
  ibv_mr *rdma_mr;
  ibv_mr *req_buf_mr;
  const RMCType workload;
  LLNode *linkedlist;
  HugeAllocator huge;

  void send_rdma_mr();

 public:
  /* TODO: move these to a config.h or something */

  HostServer(uint16_t num_qps, RMCType work)
      : rserver(num_qps, true), hsready(false), workload(work) {
    rdma_buffer = huge.get();
    req_buf = std::make_unique<CmdRequest>();

    /* HugeAllogator memsets buffer to 0. Init hostserver memory here */
    if (workload == TRAVERSE_LL || workload == LOCK_TRAVERSE_LL)
      linkedlist = create_linkedlist<LLNode>(rdma_buffer, RMCK_APPS_BUFF_SZ);
  }

  ~HostServer() {
    switch (workload) {
      case TRAVERSE_LL:
      case LOCK_TRAVERSE_LL:
        destroy_linkedlist(linkedlist);
        break;
      case RANDOM_WRITES: /* fall through */
      case HASHTABLE:
        break;
    }
  }

  void connect_and_block(int port);
  void disconnect();
};

inline void HostServer::send_rdma_mr() {
  assert(hsready);

  req_buf->type = CmdType::SET_RDMA_MR;
  memcpy(&req_buf->request.rdma_mr.mr, rdma_mr, sizeof(ibv_mr));
  rserver.post_send(rserver.get_ctrl_ctx(), req_buf.get(), sizeof(CmdRequest),
                    req_buf_mr->lkey);
  // there's only one CQ
  rserver.poll_exactly(1, rserver.get_send_cq(0));
  printf("sent SET_RDMA_MR\n");
}

#endif
