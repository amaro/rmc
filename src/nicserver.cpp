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
  int32_t hostport, clientport, llnodes, numqps;
  int c;

  opterr = 0;
  clientport = 30000;
  hostport = 30001;
  llnodes = 0;
  numqps = 0;

  /* server address, num queue pairs, number of ll nodes */
  while ((c = getopt(argc, argv, "s:q:n:")) != -1) {
    switch (c) {
    case 's':
      hostaddr = optarg;
      break;
    case 'q':
      numqps = atoi(optarg);
      break;
    case 'n':
      llnodes = atoi(optarg);
      break;
    case '?':
    default:
      std::cerr << "Usage: -s hostaddr -q numqps -n numnodes\n";
      return 1;
    }
  }

  std::cout << "hostaddr=" << hostaddr << " numqps=" << numqps
            << " llnodes=" << llnodes << "\n";

  if (llnodes == 0) {
    std::cerr << "Need to specify number of nodes with -n\n";
    return 1;
  }

  /* by pass error checking */
  if (llnodes == -1)
    llnodes = 0;

  if (numqps == 0) {
    std::cerr << "Need to specify number of qps with -q\n";
    return 1;
  }

  if (strcmp(hostaddr, "") == 0) {
    std::cerr << "Need to specify number of qps with -q\n";
    return 1;
  }

  LOG("timer freq=" << get_freq());

  OneSidedClient onesidedclient(numqps);
  RDMAServer rserver(1, false);
  NICServer nicserver(onesidedclient, rserver, QP_MAX_2SIDED_WRS);

  // RMCAllocator::init();
  RMCScheduler sched(nicserver, llnodes, numqps);

  nicserver.start(sched, hostaddr, hostport, clientport);
  // RMCAllocator::release();
  LOG("bye.");
}
