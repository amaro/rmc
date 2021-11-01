#include <unistd.h>

#include "hostserver.h"
#include "utils/utils.h"

void HostServer::connect_and_block(int port) {
  assert(!hsready);

  /* accept connection */
  rserver.connect_from_client(port);

  /* register mrs */
  rdma_mr = rserver.register_mr(
      rdma_buffer, RDMA_BUFF_SIZE,
      IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE |
          IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_RELAXED_ORDERING);

  LOG("rdma_mr rkey=" << rdma_mr->rkey);
  /* req_buf holds outgoing requests to nicserver */
  req_buf_mr = rserver.register_mr(req_buf.get(), sizeof(CmdRequest), 0);

  hsready = true;
  send_rdma_mr();

  rserver.disconnect_events();
}

void HostServer::disconnect() {
  assert(hsready);

  LOG("received disconnect req");
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
      std::cerr << "Usage: -q numqps -w workload\n";
      return 1;
    }
  }

  Workload work = READ;
  if (workload != nullptr) {
    if (strcmp(workload, "read") == 0) {
      work = READ;
    } else if (strcmp(workload, "write") == 0) {
      work = WRITE;
    } else if (strcmp(workload, "hash") == 0) {
      work = HASHTABLE;
    } else if (strcmp(workload, "log") == 0) {
      work = SHAREDLOG;
    } else {
      std::cerr << "Specify workload=read, write, hash, log\n";
      return 1;
    }
  } else {
    std::cerr << "Usage: -q numqps -w workload\n";
    return 1;
  }

  std::cout << "Workload=" << workload << "\n";

  // HostServer creates as many qps as requested and 1 cq
  // qps here are for nicserver to connect to
  current_tid = 0;
  HostServer server(numqps, work);
  server.connect_and_block(port);
}
