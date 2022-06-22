#include "hostserver.h"

#include <unistd.h>

#include "utils/utils.h"

void HostServer::connect_and_block(int port) {
  assert(!hsready);

  /* accept connection */
  rserver.connect_from_client(port);

  /* register mrs */
  rdma_mr = rserver.register_mr(
      rdma_buffer, RMCK_TOTAL_BUFF_SZ,
      IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE |
          IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_ATOMIC |
          IBV_ACCESS_RELAXED_ORDERING);

  printf("rdma_mr rkey=%u\n", rdma_mr->rkey);
  /* ctrlreq holds outgoing ctrl requests to nicserver */
  ctrlreq_mr = rserver.register_mr(ctrlreq.get(), sizeof(CtrlReq), 0);

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

  RMCType work = TRAVERSE_LL;
  if (workload != nullptr) {
    if (strcmp(workload, "readll") == 0) {
      // nothing
    } else if (strcmp(workload, "writerandom") == 0) {
      work = RANDOM_WRITES;
    } else if (strcmp(workload, "hash") == 0) {
      work = HASHTABLE;
    } else {
      die("Specify workload=readll, writerandom, hash\n");
    }
  } else {
    die("Usage: -q numqps -w workload\n");
  }

  printf("Workload=%s\n", workload);

  // HostServer creates as many qps as requested and 1 cq
  // qps here are for nicserver to connect to
  current_tid = 0;
  HostServer server(numqps, work);
  server.connect_and_block(port);
}
