#include "nicserver.h"

#include <unistd.h>

#include <thread>

#include "allocator.h"
#include "scheduler.h"
#include "utils/utils.h"

static char *hostaddr = nullptr;
static int32_t hostport, clientport, num_threads, qps_per_thread;
static RMCType work;

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

  printf("received disconnect req tid=%u\n", current_tid);
  rserver.disconnect_events();
  nsready = false;
}

void NICServer::start(RMCScheduler &sched, const unsigned int &clientport,
                      uint16_t tid) {
  puts("waiting for hostclient to connect.");
  connect(clientport);
  init(sched, tid);
}

void NICServer::post_batched_recv_req(RDMAContext &ctx, unsigned int startidx,
                                      unsigned int num_reqs) {
  assert(nsready);

  rserver.post_batched_recv(ctx, req_buf_mr, startidx, sizeof(CmdRequest),
                            num_reqs);
}

void thread_launch(OneSidedClient &osc, uint16_t thread_id,
                   pthread_barrier_t *barrier) {
  // 1 qp and 1 cq for client-nicserver communication
  current_tid = thread_id;
  printf("START thread current_tid=%u\n", current_tid);

  RDMAServer rserver(1, false);
  NICServer nicserver(osc, rserver);

  RMCScheduler sched(nicserver, work, qps_per_thread);
  pthread_barrier_wait(barrier);
  nicserver.start(sched, clientport + thread_id, thread_id);
  printf("EXIT thread current_tid=%u\n", current_tid);
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
    puts("Usage: -s hostaddr -q numqps -w workload -t numthreads");
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
#if defined(WORKLOAD_HASHTABLE)
    if (strcmp(workload, "hash") == 0) {
      work = HASHTABLE;
    } else {
      return usage();
    }
#else
    if (strcmp(workload, "readll") == 0) {
      work = TRAVERSE_LL;
    } else if (strcmp(workload, "readll_lock") == 0) {
      work = LOCKED_TRAVERSE_LL;
    } else if (strcmp(workload, "writerandom") == 0) {
      work = RANDOM_WRITES;
    } else {
      return usage();
    }
#endif
  } else {
    return usage();
  }

  if (numqps <= 0 || strcmp(hostaddr, "") == 0 || num_threads <= 0)
    return usage();

  rt_assert(numqps % num_threads == 0, "numqps%%num_threads==%d\n",
            numqps % num_threads);
  qps_per_thread = numqps / num_threads;

  printf("Workload=%s\n", workload);
  printf("hostaddr=%s numqps=%d\n", hostaddr, numqps);
  printf("threads=%d\n", num_threads);

  set_env_var("MLX5_SCATTER_TO_CQE", "1");
  set_env_var("MLX5_SINGLE_THREADED", "1");

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

  for (auto i = 0; i < num_threads; ++i) threads[i].join();

  puts("bye.");
}
