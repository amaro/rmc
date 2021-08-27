#include <unistd.h>

#include "allocator.h"
#include "nicserver.h"
#include "scheduler.h"
#include "utils/utils.h"

void NICServer::connect(const unsigned int &port) {
  assert(!nsready);
  rserver.connect_from_client(port);

  /* nic writes incoming requests */
  req_buf_mr =
      rserver.register_mr(&req_buf[0], sizeof(CmdRequest) * bsize,
                          IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_RELAXED_ORDERING);
  /* cpu writes outgoing replies */
  reply_buf_mr =
      rserver.register_mr(&reply_buf[0], sizeof(CmdReply) * bsize, 0);

  nsready = true;
}

/* not inline, but datapath inside is inline */

void NICServer::init(RMCScheduler &sched) {
  assert(nsready);

  /* handle the initial rmc get id call */
  rserver.post_batched_recv(rserver.get_ctrl_ctx(), req_buf_mr, 0,
                            sizeof(CmdRequest), 1);
  rserver.poll_exactly(1, rserver.get_recv_cq());
  sched.dispatch_new_req(get_req(0));

  /* post the initial recvs */
  rserver.post_batched_recv(rserver.get_ctrl_ctx(), req_buf_mr, 0,
                            sizeof(CmdRequest), bsize);

  sched.debug_allocate();
  sched.run();
  sched.debug_print_stats();
}

void NICServer::disconnect() {
  assert(nsready);

  LOG("received disconnect req");
  rserver.disconnect_events();
  nsready = false;
}

void NICServer::start(RMCScheduler &sched, const std::string &hostaddr,
                      const unsigned int &hostport,
                      const unsigned int &clientport) {
  LOG("connecting to hostserver.");
  onesidedclient.connect(hostaddr, hostport);

  LOG("waiting for hostclient to connect.");
  this->connect(clientport);
  this->init(sched);
}

void NICServer::post_batched_recv_req(RDMAContext &ctx, unsigned int startidx,
                                      unsigned int num_reqs) {
  assert(nsready);

  rserver.post_batched_recv(ctx, req_buf_mr, startidx, sizeof(CmdRequest),
                            num_reqs);
}

int main(int argc, char *argv[]) {
  char *hostaddr = nullptr;
  char *workload = nullptr;
  int32_t hostport, clientport, numqps;
  int c;
  Workload work;

  opterr = 0;
  clientport = 30000;
  hostport = 30001;
  numqps = 0;

  /* server address, num queue pairs */
  while ((c = getopt(argc, argv, "s:q:w:")) != -1) {
    switch (c) {
    case 's':
      hostaddr = optarg;
      break;
    case 'q':
      numqps = atoi(optarg);
      break;
    case 'w':
      workload = optarg;
      break;
    case '?':
    default:
      std::cerr << "Usage: -s hostaddr -q numqps\n";
      return 1;
    }
  }

  if (workload != nullptr) {
    if (strcmp(workload, "read") == 0) {
      work = READ;
    } else if (strcmp(workload, "write") == 0) {
      work = WRITE;
    } else {
      std::cerr << "Specify workload=read, write\n";
      return 1;
    }
  } else {
    std::cerr << "Specify workload=read, write\n";
    return 1;
  }

  std::cout << "Workload=" << workload << "\n";

  if (numqps <= 0) {
    std::cerr << "Need to specify number of qps with -q\n";
    return 1;
  }

  if (strcmp(hostaddr, "") == 0) {
    std::cerr << "Need to specify number of qps with -q\n";
    return 1;
  }

  std::cout << "hostaddr=" << hostaddr << " numqps=" << numqps << "\n";

  OneSidedClient onesidedclient(numqps);
  RDMAServer rserver(1, false);
  NICServer nicserver(onesidedclient, rserver, QP_MAX_2SIDED_WRS);

  RMCScheduler sched(nicserver, numqps, work);
  nicserver.start(sched, hostaddr, hostport, clientport);
  LOG("bye.");
}
