#include "client.h"

#include <pthread.h>
#include <unistd.h>

#include <algorithm>
#include <fstream>
#include <queue>
#include <thread>
#include <vector>

#include "rpc.h"
#include "utils/cxxopts.h"
#include "utils/utils.h"

static constexpr int NUM_REPS = 1;
static constexpr const uint32_t NUM_REQS = 100000;
static std::string server, mode, rmc;
static int numaccess;
static unsigned int start_port;
static float load = 0.0;

void HostClient::connect(const std::string &ip, const unsigned int &port) {
  assert(!rmccready);
  rclient.connect_to_server(ip, port);

  req_buf_mr =
      rclient.register_mr(&datareqs[0], sizeof(DataReq) * QP_MAX_2SIDED_WRS, 0);
  reply_buf_mr = rclient.register_mr(
      &datareplies[0], sizeof(DataReply) * QP_MAX_2SIDED_WRS,
      IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_RELAXED_ORDERING);
  rmccready = true;
}

// kept here as example on how to issue one req at a time
// RMCId HostClient::get_rmc_id(const RMC &rmc) {
//  assert(rmccready);
//
//  CmdRequest *req = get_req(0);
//  CmdReply *reply = get_reply(0);
//
//  post_recv_reply(reply);
//
//  /* get_id request */
//  req->type = CmdType::GET_RMCID;
//  rmc.copy(req->request.getid.rmc, sizeof(req->request.getid.rmc));
//  post_send_req(req);
//
//  rclient.poll_exactly(1, rclient.get_send_cq(0));
//  rclient.poll_exactly(1, rclient.get_recv_cq(0));
//
//  /* read CmdReply */
//  assert(reply->type == CmdType::GET_RMCID);
//  return reply->reply.getid.id;
//}

// tid here is only for debugging purposes
long long HostClient::do_maxinflight(uint32_t num_reqs, uint32_t param,
                                     pthread_barrier_t *barrier, uint16_t tid) {
  assert(rmccready);

  const auto maxinflight = get_max_inflight();
  // we have one HostClient per thread, so CQs 0 are the only ones that exist
  ibv_cq_ex *recv_cq = rclient.get_recv_cq(0);
  ibv_cq_ex *send_cq = rclient.get_send_cq(0);
  long long duration = 0;
  static auto noop = [](size_t) constexpr->void{};

  for (auto i = 0u; i < maxinflight; i++) arm_call_req(get_req(i), param);

  pthread_barrier_wait(barrier);

  time_point start = time_start();
  for (auto i = 0u; i < num_reqs; i++) {
    /* send as many requests as we have credits for */
    if (this->inflight < maxinflight) {
      post_recv_reply(get_reply(this->req_idx));
      post_send_req_unsig(get_req(this->req_idx));
      this->inflight++;
      inc_with_wraparound(this->req_idx, maxinflight);
    }

    maybe_poll_sends(send_cq);
    if (this->inflight > 0) {
      uint32_t polled = 0;
      if (this->inflight < maxinflight)
        polled = rclient.poll_atmost(this->inflight, recv_cq, noop);
      else
        polled = rclient.poll_atleast(1, recv_cq, noop);

      this->inflight -= polled;
    }
  }

  if (this->inflight > 0)
    this->inflight -= rclient.poll_atleast(this->inflight, recv_cq, noop);
  duration = time_end(start);

  assert(this->inflight == 0);
  for (auto i = 0u; i < std::min(maxinflight, num_reqs); i++)
    assert(*(reinterpret_cast<int *>(datareplies[i].data.exec.data)) == 1);

  return duration;
}

static void get_send_times_exp(std::vector<long long> &send_cycles,
                               uint32_t max_num_req, uint64_t mean_interval_ns,
                               long long freq) {
  send_cycles.reserve(max_num_req);
  std::default_random_engine generator;
  std::exponential_distribution<double> distribution(double(1) /
                                                     mean_interval_ns);
  long long timestamp = 0;

  for (auto i = 0u; i < max_num_req; i++) {
    timestamp = distribution(generator);
    send_cycles.push_back(ns_to_cycles(timestamp, freq));
  }
}

int HostClient::do_load(float load, std::vector<uint32_t> &rtts,
                        uint32_t num_reqs, long long freq, uint32_t numaccesses,
                        pthread_barrier_t *barrier) {
  assert(rmccready);

  uint32_t rtt_idx = 0;
  uint32_t curr_max_inflight = 0;
  uint32_t late = 0;
  const uint64_t wait_in_nsec = load * 1000;
  const auto maxinflight = get_max_inflight();
  long long max_late_cycles = 0;
  std::queue<long long> start_times;
  static auto noop = [](size_t) constexpr->void{};
  long long ts_start_rtt = 0;
  ibv_cq_ex *recv_cq = rclient.get_recv_cq(0);
  ibv_cq_ex *send_cq = rclient.get_send_cq(0);

  printf("will issue %u requests every %lu nanoseconds\n", num_reqs,
         wait_in_nsec);

  for (auto i = 0u; i < maxinflight; i++) arm_call_req(get_req(i), numaccesses);

  std::vector<long long> send_cycles;
  get_send_times_exp(send_cycles, num_reqs, wait_in_nsec, freq);

  pthread_barrier_wait(barrier);
  long long next_send = get_cycles();
  for (auto i = 0u; i < num_reqs; i++) {
    next_send += send_cycles[i];

    /* must start the RTT here because it might get delayed if there's too many
     * reqs in flight */
    post_recv_reply(get_reply(this->req_idx));
    ts_start_rtt = get_cycles();
    start_times.push(ts_start_rtt);

    /* if there are more inflight requests than the max, we can only poll here
       for a reply to arrive */
    if (this->inflight == maxinflight)
      load_handle_reps(start_times, rtts,
                       rclient.poll_atleast(1, recv_cq, noop), rtt_idx);

    /* there's space to send new req, so send new one */
    load_send_request();

    /* was the *client* late? */
    if (ts_start_rtt > next_send) {
      late++;
      max_late_cycles = std::max(ts_start_rtt - next_send, max_late_cycles);
    }

    curr_max_inflight = std::max(curr_max_inflight, this->inflight);

    /* regular poll for sends and replies */
    maybe_poll_sends(send_cq);
    if (this->inflight > 0)
      load_handle_reps(start_times, rtts,
                       rclient.poll_atmost(this->inflight, recv_cq, noop),
                       rtt_idx);

    /* wait until next_send */
    while (get_cycles() < next_send) {
      if (this->inflight > 0) {
        const uint32_t polled = rclient.poll_atmost(1, recv_cq, noop);
        load_handle_reps(start_times, rtts, polled, rtt_idx);
      }

      _mm_pause();
    }
  }

  maybe_poll_sends(send_cq);
  if (this->inflight > 0) {
    const uint32_t polled = rclient.poll_atleast(this->inflight, recv_cq, noop);
    load_handle_reps(start_times, rtts, polled, rtt_idx);
  }

  printf("max concurrent=%u\n", curr_max_inflight);
  printf("late=%u\n", late);
  printf("max late ns=%lld\n", cycles_to_ns(max_late_cycles, freq));
  return 0;
}

void HostClient::load_send_request() {
  post_send_req_unsig(get_req(this->req_idx));
  this->inflight++;
  inc_with_wraparound(this->req_idx, get_max_inflight());
}

void HostClient::load_handle_reps(std::queue<long long> &start_times,
                                  std::vector<uint32_t> &rtts, uint32_t polled,
                                  uint32_t &rtt_idx) {
  if (polled == 0) return;

  long long end = get_cycles();

  for (auto reply = 0u; reply < polled; ++reply) {
    rtts[rtt_idx++] = end - start_times.front();
    start_times.pop();
  }

  this->inflight -= polled;
}

void HostClient::last_cmd() {
  assert(rmccready);

  DataReq *req = get_req(0);
  req->type = DataCmdType::LAST_CMD;
  post_send_req(req);
  rclient.poll_exactly(1, rclient.get_send_cq(0));
  disconnect();
}

void HostClient::disconnect() {
  assert(rmccready);
  rclient.disconnect_all();
  rmccready = false;
}

/* control plane */
void HostClient::initialize_rmc(RMCType type) {
  assert(rmccready);

  DataReq *req = get_req(0);
  req->type = DataCmdType::INIT_RMC;

  const DataReply *reply = get_reply(0);

  post_recv_reply(reply);
  post_send_req(req);

  rclient.poll_exactly(1, rclient.get_send_cq(0));
  rclient.poll_exactly(1, rclient.get_recv_cq(0));

  /* read reply */
  rt_assert(reply->type == DataCmdType::INIT_RMC,
            "during rmc init, expected INIT_RMC reply");
  const InitReply *initreply = &reply->data.init;
  printf("rbaseaddr=%lu\nlength=%u\nrkey=%u\n", initreply->rbaseaddr,
         initreply->length, initreply->rkey);
}

template <typename T>
double get_avg(std::vector<T> &durations) {
  double avg = 0;
  T sum = 1;

  for (T &d : durations) sum += d;

  avg = sum / (double)durations.size();
  return avg;
}

double get_median(const std::vector<long long> &durations) {
  double median = 0;
  size_t vecsize = 0;

  vecsize = durations.size();
  if (durations.size() % 2 == 0)
    median =
        (durations[vecsize / 2] + durations[(vecsize / 2) - 1]) / (double)2;
  else
    median = durations[vecsize / 2];

  return median;
}

void print_stats_maxinflight(std::vector<long long> &durations,
                             int maxinflight) {
  double avg = 0;
  double median = 0;

  std::sort(durations.begin(), durations.end());

  avg = get_avg(durations);
  median = get_median(durations);

  std::cout << "NUM_REPS=" << NUM_REPS << "\n";
  std::cout << "max inflight=" << maxinflight << "\n";
  std::cout << "avg=" << std::fixed << avg << "\n";
  std::cout << "median=" << median << "\n";
}

double print_stats_load(std::vector<uint32_t> &durations, long long freq) {
  double median = 0;

  for (auto &d : durations) d = cycles_to_ns(d, freq);

  unsigned int remove = durations.size() * 0.1;
  auto d2 = std::vector<long long>(durations.begin() + remove, durations.end());
  std::sort(d2.begin(), d2.end());
  median = get_median(d2);
  return median;
}

/* keeps maxinflight active requests at all times */
double benchmark_maxinflight(HostClient &client, uint32_t param,
                             pthread_barrier_t *barrier, uint16_t tid,
                             uint32_t num_reqs) {
  const uint32_t max = client.get_max_inflight();
  double duration;

  printf("get_max_inflight()=%u\n", max);
  printf("maxinflight: warming up\n");
  client.do_maxinflight(max * 10, param, barrier, tid);

  printf("maxinflight: benchmark start\n");
  duration = client.do_maxinflight(num_reqs, param, barrier, tid);
  printf("maxinflight: benchmark end\n");

  client.last_cmd();
  return duration;
  // print_stats_maxinflight(durations, maxinflight);
}

double benchmark_load(HostClient &client, uint32_t numaccesses, float load,
                      pthread_barrier_t *barrier, std::vector<uint32_t> &rtts,
                      uint32_t num_reqs) {
  const uint32_t max = client.get_max_inflight();
  const long long freq = get_freq();

  printf("get_max_inflight()=%d\n", max);
  printf("rdtsc freq=%lld\n", freq);

  printf("load: warming up\n");
  client.do_load(load, rtts, max, freq, numaccesses, barrier);

  printf("load: benchmark start\n");
  client.do_load(load, rtts, num_reqs, freq, numaccesses, barrier);
  printf("load: benchmark end\n");
  client.last_cmd();

  return print_stats_load(rtts, freq);
}

void thread_launch_maxinflight(uint16_t thread_id, pthread_barrier_t *barrier,
                               std::vector<double> &durations, int num_threads,
                               RMCType workload) {
  // we create one HostClient per thread, so in each client we have 1 QP and 1
  // CQ. therefore, we don't need thread_ids to select QPs and CQs here
  printf("START thread=%d\n", thread_id);
  current_tid = 0;
  HostClient client(workload);
  client.connect(server, start_port + thread_id);

  if (thread_id == 0) client.initialize_rmc(workload);

  durations[thread_id] = benchmark_maxinflight(
      client, numaccess, barrier, thread_id, NUM_REQS / num_threads);

  pthread_barrier_wait(barrier);
  if (thread_id == 0) {
    double max = 0;
    for (double &d : durations) max = std::max(d, max);

    double ops = NUM_REQS / (max / (double)1000000000);
    std::cout << "max duration=" << max << "\n";
    std::cout << "num_reqs per thread=" << NUM_REQS / num_threads << "\n";
    std::cout << "num_reqs=" << NUM_REQS << "\n";
    std::cout << "Ops per sec=" << std::fixed << ops << "\n";
  }
}

void thread_launch_load(uint16_t tid, pthread_barrier_t *barrier,
                        std::vector<std::vector<uint32_t>> &rtts,
                        int num_threads, RMCType workload) {
  // we create one HostClient per thread, so in each client we have 1 QP and 1
  // CQ. therefore, we don't need thread_ids to select QPs and CQs here
  current_tid = 0;
  HostClient client(workload);
  client.connect(server, start_port + tid);

  if (tid == 0) client.initialize_rmc(workload);

  benchmark_load(client, numaccess, load, barrier, rtts[tid],
                 NUM_REQS / num_threads);

  pthread_barrier_wait(barrier);
  if (tid == 0) {
    // print all rtts
    std::cout << "start rtts\n";
    size_t num_threads = rtts.size();
    for (auto i = 0u; i < num_threads; ++i)
      for (const auto &rtt : rtts[i]) std::cout << rtt << "\n";
    std::cout << "end rtts\n";
  }
}

int main(int argc, char *argv[]) {
  cxxopts::Options opts("client", "RMC client");

  // clang-format off
  opts.add_options()
    ("s,server", "nicserver address", cxxopts::value<std::string>())
    ("p,port", "nicserver start port", cxxopts::value<int>()->default_value("30000"))
    ("o,output", "output file", cxxopts::value<std::string>())
    ("mode", "client mode, can be: maxinflight or load", cxxopts::value<std::string>())
    ("l,load", "send 1 new req every these many microseconds (for mode=load)",
      cxxopts::value<float>()->default_value("0.0"))
    ("t, threads", "number of threads", cxxopts::value<int>())
    ("rmc", "rmc workload; choose: readll, readll_lock, writerandom, hash",
      cxxopts::value<std::string>())
    ("numaccess", "number of accesses per rmc request for readll, readll_lock, writerandom",
      cxxopts::value<int>()->default_value("0"))
    ("h,help", "Print usage");
  // clang-format on

  int num_threads;

  try {
    auto result = opts.parse(argc, argv);

    if (result.count("help")) die("%s\n", opts.help().c_str());

    server = result["server"].as<std::string>();
    start_port = result["port"].as<int>();
    mode = result["mode"].as<std::string>();
    load = result["load"].as<float>();
    num_threads = result["threads"].as<int>();
    rmc = result["rmc"].as<std::string>();
    numaccess = result["numaccess"].as<int>();

    if (mode != "load" && mode != "maxinflight") {
      std::cerr << "need to specify mode: load or maxinflight\n";
      die("%s\n", opts.help().c_str());
    } else if (mode == "load" && load <= 0) {
      die("mode=load requires load > 0\n");
    } else if (rmc != "readll" && rmc != "readll_lock" &&
               rmc != "writerandom" && rmc != "hash") {
      die("rmc can only be: readll, readll_lock, writerandom, hash\n");
    } else if ((rmc == "readll" || rmc == "readll_locked" ||
                rmc == "writerandom") &&
               numaccess < 0) {
      die("for rmcs (readll, readll_locked, writerandom) numaccess must be > "
          "0\n");
    } else if (num_threads <= 0) {
      die("threads must be > 0\n");
    }
  } catch (const std::exception &e) {
    std::cerr << e.what() << "\n";
    die("%s\n", opts.help().c_str());
  }

  set_env_var("MLX5_SINGLE_THREADED", "1");

  std::vector<std::thread> threads;
  pthread_barrier_t barrier;
  TEST_NZ(pthread_barrier_init(&barrier, nullptr, num_threads));

  printf("will launch %d threads\n", num_threads);

  // for load; one rtt vector per thread
  std::vector<std::vector<uint32_t>> rtts(
      num_threads, std::vector<uint32_t>(NUM_REQS / num_threads));

  // for maxinflight; one duration per thread
  std::vector<double> durations(num_threads);

  RMCType workload;
  if (rmc == "readll")
    workload = TRAVERSE_LL;
  else if (rmc == "readll_lock")
    workload = LOCK_TRAVERSE_LL;
  else if (rmc == "writerandom")
    workload = RANDOM_WRITES;
  else if (rmc == "hash")
    workload = HASHTABLE;
  else
    die("bad rmc\n");
  printf("workload set to=%s\n", rmc.c_str());

  if (mode == "load") {
    // distribute load request evenly among all cores
    if (num_threads > 1) {
      load = load * num_threads;
      printf("adjusting load using multiple cores\n");
      printf("each core will issue a req every %f us\n", load);
    }

    for (auto i = 0; i < num_threads; ++i) {
      std::thread t(thread_launch_load, i, &barrier, std::ref(rtts),
                    num_threads, workload);
      threads.push_back(std::move(t));
    }
  } else {
    for (auto i = 0; i < num_threads; ++i) {
      std::thread t(thread_launch_maxinflight, i, &barrier, std::ref(durations),
                    num_threads, workload);
      threads.push_back(std::move(t));
    }
  }

  for (auto i = 0; i < num_threads; ++i) threads[i].join();
}
