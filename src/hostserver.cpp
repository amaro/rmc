#include "hostserver.h"

#include <unistd.h>

#include "utils/utils.h"

void HostServer::connect_and_block(int port) {
  assert(!hsready);

  /* accept connection */
  rserver.connect_from_client(port);

  /* ctrlreq holds outgoing ctrl requests to nicserver */
  ctrlreq_mr = rserver.register_mr(ctrlreq.get(), sizeof(CtrlReq), 0);

  /* init per-rmc server buffers */
  rmcs_server_init(SA, allocs);

  hsready = true;
  send_rdma_mr();
  rserver.disconnect_events();
}

void HostServer::disconnect() {
  assert(hsready);

  printf("received disconnect req\n");
  rserver.disconnect_events();
  hsready = false;
}

/* send rdma mr info to nicserver */
void HostServer::send_rdma_mr() {
  assert(hsready);

  ctrlreq->type = CtrlCmdType::RDMA_MR;
  ctrlreq->data.mr.num_mr = 1;

  for (auto i = 0u; i < allocs.size(); ++i)
    memcpy(&ctrlreq->data.mr.mrs[i], allocs[i].mr, sizeof(ibv_mr));

  printf("will send info about %ld memory regions\n", allocs.size());
  rserver.post_send(rserver.get_ctrl_ctx(), ctrlreq.get(), sizeof(CtrlReq),
                    ctrlreq_mr->lkey);
  // there's only one CQ
  rserver.poll_exactly(1, rserver.get_send_cq(0));
  printf("sent RDMA_MR\n");
}

int main(int argc, char *argv[]) {
  int32_t port, numqps;
  char *workload = nullptr;
  int c;

  opterr = 0;
  port = 10000;
  numqps = 0;

  /* num queue pairs, workload */
  while ((c = getopt(argc, argv, "q:w:")) != -1) {
    switch (c) {
      case 'q':
        numqps = atoi(optarg);
        break;
      case 'w':
        workload = optarg;
        break;
      case '?':
      default:
        die("Usage: -q numqps -w workload\n");
        return 1;
    }
  }

  printf("FIX Workload not needed anymore=%s\n", workload);

  // HostServer creates as many qps as requested and 1 cq
  // qps here are for nicserver to connect to
  current_tid = 0;
  HostServer server(numqps);
  server.connect_and_block(port);
}
