#include <thread>
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
      rserver.register_mr(&req_buf[0], sizeof(CmdRequest) * QP_MAX_2SIDED_WRS,
                          IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_RELAXED_ORDERING);
  /* cpu writes outgoing replies */
  reply_buf_mr = rserver.register_mr(&reply_buf[0],
                                     sizeof(CmdReply) * QP_MAX_2SIDED_WRS, 0);

  nsready = true;
}

/* not inline, but datapath inside is inline */
void NICServer::init(RMCScheduler &sched, uint16_t tid) {
  assert(nsready);

  /* handle the initial rmc get id call */
  rserver.post_batched_recv(rserver.get_ctrl_ctx(), req_buf_mr, 0,
                            sizeof(CmdRequest), 1);
  rserver.poll_exactly(1, rserver.get_recv_cq(0));
  sched.dispatch_new_req(get_req(0));

  /* post the initial recvs */
  rserver.post_batched_recv(rserver.get_ctrl_ctx(), req_buf_mr, 0,
                            sizeof(CmdRequest), QP_MAX_2SIDED_WRS);

  sched.debug_allocate();
  sched.run();
  sched.debug_print_stats();
}

void NICServer::disconnect() {
  assert(nsready);

  LOG("received disconnect req tid=" << current_tid);
  rserver.disconnect_events();
  nsready = false;
}

void NICServer::start(RMCScheduler &sched, const unsigned int &clientport,
                      uint16_t tid) {
  LOG("waiting for hostclient to connect.");
  connect(clientport);
  init(sched, tid);
}

void NICServer::post_batched_recv_req(RDMAContext &ctx, unsigned int startidx,
                                      unsigned int num_reqs) {
  assert(nsready);

  rserver.post_batched_recv(ctx, req_buf_mr, startidx, sizeof(CmdRequest),
                            num_reqs);
}

char *hostaddr = nullptr;
int32_t hostport, clientport, num_threads, qps_per_thread;
Workload work;

void thread_launch(OneSidedClient &osc, uint16_t thread_id,
                   pthread_barrier_t *barrier) {
  // 1 qp and 1 cq for client-nicserver communication
  current_tid = thread_id;
  LOG("START thread current_tid=" << current_tid);

  RDMAServer rserver(1, false);
  NICServer nicserver(osc, rserver);

  RMCScheduler sched(nicserver, work, qps_per_thread);
  pthread_barrier_wait(barrier);
  nicserver.start(sched, clientport + thread_id, thread_id);
  LOG("EXIT thread current_tid=" << current_tid);
}

int main(int argc, char *argv[]) {
  char *workload = nullptr;
  int32_t numqps;
  int c;

  opterr = 0;
  clientport = 30000;
  hostport = 10000;
  numqps = 0;

  auto usage = []() -> int {
    std::cerr << "Usage: -s hostaddr -q numqps -w workload -t numthreads\n";
    return 1;
  };

  /* server address, num queue pairs, workload, threads */
  while ((c = getopt(argc, argv, "s:q:w:t:")) != -1) {
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
    case 't':
      num_threads = atoi(optarg);
      break;
    case '?':
    default:
      return usage();
    }
  }

  if (workload != nullptr) {
    if (strcmp(workload, "read") == 0) {
      work = READ;
    } else if (strcmp(workload, "write") == 0) {
      work = WRITE;
    } else {
      return usage();
    }
  } else {
    return usage();
  }

  if (numqps <= 0 || strcmp(hostaddr, "") == 0 || num_threads <= 0)
    return usage();

  if (numqps % num_threads != 0) {
    std::cerr << "number of qps \% num_threads must equal 0\n";
    return 1;
  }
  qps_per_thread = numqps / num_threads;

  LOG("Workload=" << workload);
  LOG("hostaddr=" << hostaddr << " numqps=" << numqps);
  LOG("threads=" << num_threads);

  std::vector<std::thread> threads;
  pthread_barrier_t barrier;
  TEST_NZ(pthread_barrier_init(&barrier, nullptr, num_threads));

  // we create numqps QPs, and num_threads CQs in a single OneSidedClient
  // object. we will use numqps/num_threads QPs and 1 CQ per thread
  OneSidedClient onesidedclient(numqps, num_threads);
  onesidedclient.connect(hostaddr, hostport);

  for (auto i = 0; i < num_threads; ++i) {
    std::thread t(thread_launch, std::ref(onesidedclient), i, &barrier);
    threads.push_back(std::move(t));
  }

  for (auto i = 0; i < num_threads; ++i)
    threads[i].join();

  LOG("bye.");
}
