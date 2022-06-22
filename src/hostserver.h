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
  std::unique_ptr<CtrlReq> ctrlreq;
  // no reply_buf since we don't need one right now.
  ibv_mr *rdma_mr;
  ibv_mr *ctrlreq_mr;
  const RMCType workload;
  LLNode *linkedlist;
  HugeAllocator huge;

  void send_rdma_mr();

 public:
  /* TODO: move these to a config.h or something */

  HostServer(uint16_t num_qps, RMCType work)
      : rserver(num_qps, true), hsready(false), workload(work) {
    rdma_buffer = huge.get();
    ctrlreq = std::make_unique<CtrlReq>();

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

/* send rdma mr info to nicserver */
inline void HostServer::send_rdma_mr() {
  assert(hsready);

  ctrlreq->type = CtrlCmdType::RDMA_MR;
  ctrlreq->data.mr.num_mr = 1;
  memcpy(&ctrlreq->data.mr.mrs[0], rdma_mr, sizeof(ibv_mr));

  rserver.post_send(rserver.get_ctrl_ctx(), ctrlreq.get(), sizeof(CtrlReq),
                    ctrlreq_mr->lkey);
  // there's only one CQ
  rserver.poll_exactly(1, rserver.get_send_cq(0));
  printf("sent RDMA_MR\n");
}

#endif
