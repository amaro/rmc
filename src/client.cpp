#include <algorithm>
#include <emmintrin.h>
#include <fstream>
#include <queue>
#include <unistd.h>
#include <vector>

#include "client.h"
#include "rmc.h"
#include "utils/cxxopts.h"
#include "utils/logger.h"
#include "utils/utils.h"

static constexpr int NUM_REPS = 1;
static constexpr const uint32_t NUM_REQS = 50000;

void HostClient::connect(const std::string &ip, const unsigned int &port) {
  assert(!rmccready);
  rclient.connect_to_server(ip, port);

  req_buf_mr = rclient.register_mr(&req_buf[0], sizeof(CmdRequest) * bsize, 0);
  reply_buf_mr =
      rclient.register_mr(&reply_buf[0], sizeof(CmdReply) * bsize,
                          IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_RELAXED_ORDERING);
  rmccready = true;
}

/* get_rmc_id() doesn't batch requests */
RMCId HostClient::get_rmc_id(const RMC &rmc) {
  assert(rmccready);

  CmdRequest *req = get_req(0);
  CmdReply *reply = get_reply(0);

  post_recv_reply(reply);

  /* get_id request */
  req->type = CmdType::GET_RMCID;
  rmc.copy(req->request.getid.rmc, sizeof(req->request.getid.rmc));
  post_send_req(req);

  rclient.poll_exactly(1, rclient.get_send_cq());
  rclient.poll_exactly(1, rclient.get_recv_cq());

  /* read CmdReply */
  assert(reply->type == CmdType::GET_RMCID);
  return reply->reply.getid.id;
}

long long HostClient::do_maxinflight(uint32_t num_reqs) {
  assert(rmccready);

  const auto maxinflight = get_max_inflight();
  ibv_cq_ex *recv_cq = rclient.get_recv_cq();
  ibv_cq_ex *send_cq = rclient.get_send_cq();
  long long duration = 0;
  static auto noop = [](size_t) constexpr->void{};

  for (auto i = 0u; i < maxinflight; i++)
    arm_call_req(get_req(i));

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
  double ops = num_reqs / (duration / (double)1000000000);
  LOG("duration=" << duration);
  LOG("num_reqs=" << num_reqs);
  LOG("Ops per sec=" << std::fixed << ops);
  return duration;
}

int HostClient::do_load(float load, std::vector<uint32_t> &rtts,
                        uint32_t num_reqs, long long freq) {
  assert(rmccready);

  uint32_t rtt_idx = 0;
  uint32_t current_max = 0;
  uint32_t late = 0;
  const uint64_t wait_in_nsec = load * 1000;
  const long long wait_in_cycles = ns_to_cycles(wait_in_nsec, freq);
  const auto maxinflight = get_max_inflight();
  long long max_late_cycles = 0;
  std::queue<long long> start_times;
  static auto noop = [](size_t) constexpr->void{};
  long long sent_at = 0;
  ibv_cq_ex *recv_cq = rclient.get_recv_cq();
  ibv_cq_ex *send_cq = rclient.get_send_cq();

  LOG("will issue " << num_reqs << " requests every " << wait_in_nsec
                    << " nanoseconds");
  LOG("or every " << wait_in_cycles << " cycles");

  for (auto i = 0u; i < maxinflight; i++)
    arm_call_req(get_req(i));

  long long next_send = get_cycles();
  for (auto i = 0u; i < num_reqs; i++) {
    next_send += wait_in_cycles;
    sent_at = load_send_request(start_times);

    if (sent_at > next_send) {
      late++;
      max_late_cycles = std::max(sent_at - next_send, max_late_cycles);
    }

    current_max = std::max(current_max, this->inflight);
    if (this->inflight > maxinflight) {
      std::cout << "curr_inflight=" << this->inflight
                << " maxinflight=" << maxinflight << "\n";
      break;
    }

    /* poll */
    maybe_poll_sends(send_cq);
    if (this->inflight > 0) {
      const uint32_t polled =
          rclient.poll_atmost(this->inflight, recv_cq, noop);
      load_handle_reps(start_times, rtts, polled, rtt_idx);
    }

    /* wait */
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

  LOG("max concurrent=" << current_max);
  LOG("late=" << late);
  LOG("max late ns=" << cycles_to_ns(max_late_cycles, freq));
  return 0;
}

long long HostClient::load_send_request(std::queue<long long> &start_times) {
  long long cycles_submitted;

  post_recv_reply(get_reply(this->req_idx));
  post_send_req_unsig(get_req(this->req_idx));
  cycles_submitted = get_cycles();
  start_times.push(cycles_submitted);
  this->inflight++;
  inc_with_wraparound(this->req_idx, get_max_inflight());

  return cycles_submitted;
}

void HostClient::load_handle_reps(std::queue<long long> &start_times,
                                  std::vector<uint32_t> &rtts, uint32_t polled,
                                  uint32_t &rtt_idx) {
  if (polled == 0)
    return;

  long long end = get_cycles();

  for (auto reply = 0u; reply < polled; ++reply) {
    rtts[rtt_idx++] = end - start_times.front();
    start_times.pop();
  }

  this->inflight -= polled;
}

void HostClient::last_cmd() {
  assert(rmccready);

  CmdRequest *req = get_req(0);
  req->type = CmdType::LAST_CMD;
  post_send_req(req);
  rclient.poll_exactly(1, rclient.get_send_cq());
  disconnect();
}

void HostClient::disconnect() {
  assert(rmccready);
  rclient.disconnect_all();
  rmccready = false;
}

double get_avg(std::vector<long long> &durations) {
  double avg = 0;
  long long sum = 1;

  for (long long &d : durations)
    sum += d;

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

void print_stats_load(std::vector<uint32_t> &durations, long long freq) {
  double avg = 0;
  double median = 0;

  for (auto &d : durations)
    d = cycles_to_ns(d, freq);

  unsigned int remove = durations.size() * 0.1;
  auto d2 = std::vector<long long>(durations.begin() + remove, durations.end());
  std::sort(d2.begin(), d2.end());
  avg = get_avg(d2);
  median = get_median(d2);

  std::cout << "avg (skipped first " << remove << ")=" << std::fixed << avg
            << "\n";
  std::cout << "median (skipped first " << remove << ")=" << median << "\n";

  std::cout << "rtt\n";
  for (const auto &d :
       std::vector<long long>(durations.begin() + remove, durations.end()))
    std::cout << d << "\n";
}

/* keeps maxinflight active requests at all times */
void benchmark_maxinflight(HostClient &client, std::string ofile) {
  const uint32_t max = client.get_max_inflight();
  const long long freq = get_freq();

  LOG("get_max_inflight()=" << max);
  LOG("rdtsc freq=" << freq);

  //#LOG("maxinflight: warming up");
  //#client.do_maxinflight(client.get_max_inflight() * 10);

  LOG("maxinflight: benchmark start");
  client.do_maxinflight(NUM_REQS);
  LOG("maxinflight: benchmark end");

  client.last_cmd();

  // print_stats_maxinflight(durations, maxinflight);
}

void benchmark_load(HostClient &client, float load) {
  std::vector<uint32_t> rtts(NUM_REQS);
  const uint32_t max = client.get_max_inflight();
  const long long freq = get_freq();

  LOG("get_max_inflight()=" << max);
  LOG("rdtsc freq=" << freq);

  LOG("load: warming up");
  client.do_load(load, rtts, max, freq);

  LOG("load: benchmark start");
  client.do_load(load, rtts, NUM_REQS, freq);
  LOG("load: benchmark end");
  client.last_cmd();

  print_stats_load(rtts, freq);
}

int main(int argc, char *argv[]) {
  cxxopts::Options opts("client", "RMC client");

  opts.add_options()("s,server", "nicserver address",
                     cxxopts::value<std::string>())(
      "p,port", "nicserver port",
      cxxopts::value<int>()->default_value("30000"))(
      "o,output", "output file", cxxopts::value<std::string>())(
      "mode", "client mode, can be: maxinflight or load",
      cxxopts::value<std::string>())(
      "l,load", "send 1 new req every these many microseconds (for mode=load)",
      cxxopts::value<float>()->default_value("0.0"))("h,help", "Print usage");

  std::string server, ofile, mode;
  unsigned int port;
  float load = 0.0;

  try {
    auto result = opts.parse(argc, argv);

    if (result.count("help"))
      die(opts.help());

    server = result["server"].as<std::string>();
    port = result["port"].as<int>();
    ofile = result["output"].as<std::string>();
    mode = result["mode"].as<std::string>();
    load = result["load"].as<float>();

    if (mode != "load" && mode != "maxinflight") {
      std::cerr << "need to specify mode: load or maxinflight\n";
      die(opts.help());
    } else if (mode == "load" && load == 0) {
      die("mode=load requires load > 0");
    }
  } catch (const std::exception &e) {
    std::cerr << e.what() << "\n";
    die(opts.help());
  }

  HostClient client(QP_MAX_2SIDED_WRS, 1);
  client.connect(server, port);

  if (mode == "maxinflight")
    benchmark_maxinflight(client, ofile);
  else
    benchmark_load(client, load);
}
