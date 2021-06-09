#include <fstream>
#include <algorithm>
#include <vector>
#include <queue>
#include <unistd.h>
#include "utils/cxxopts.h"
#include "client.h"
#include "rmc.h"
#include "utils/utils.h"
#include "utils/logger.h"

static constexpr int NUM_REPS = 1;
static constexpr uint32_t LOAD_NUM_REQS = 1000000;

void HostClient::connect(const std::string &ip, const unsigned int &port)
{
    assert(!rmccready);
    rclient.connect_to_server(ip, port);

    req_buf_mr = rclient.register_mr(&req_buf[0], sizeof(CmdRequest)*bsize, 0);
    reply_buf_mr = rclient.register_mr(&reply_buf[0], sizeof(CmdReply)*bsize,
                                        IBV_ACCESS_LOCAL_WRITE);
    rmccready = true;
}

/* get_rmc_id() doesn't batch requests */
RMCId HostClient::get_rmc_id(const RMC &rmc)
{
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

int HostClient::do_maxinflight(long long &duration, int maxinflight)
{
    assert(rmccready);

    int curr_inflight = 0;
    int polled = 0;
    int buf_idx = 0;
    int total_reqs = 1000;

    if (maxinflight > (int) RDMAPeer::MAX_UNSIGNALED_SENDS)
        die("max in flight > MAX_UNSIGNALED_SENDS");

    auto noop = [](size_t) -> void {};

    time_point start = time_start();
    for (auto i = 0; i < total_reqs; i++) {
        /* send as many requests as we have credits for */
        if (curr_inflight < maxinflight) {
            post_recv_reply(get_reply(buf_idx));
            arm_call_req(get_req(buf_idx));
            post_send_req_unsig(get_req(buf_idx));
            curr_inflight++;
            buf_idx = (buf_idx + 1) % maxinflight;
        }

        polled = 0;
        if (curr_inflight > 0) {
            if (curr_inflight < maxinflight)
                polled = rclient.poll_atmost(maxinflight, rclient.get_recv_cq(), noop);
            else
                polled = rclient.poll_atleast(1, rclient.get_recv_cq(), noop);
        }

        curr_inflight -= polled;
    }

    if (curr_inflight > 0)
        rclient.poll_exactly(curr_inflight, rclient.get_recv_cq());
    duration = time_end(start);

    /* check CmdReplys */
    for (auto i = 0; i < maxinflight; ++i) {
        CmdReply *reply = get_reply(i);
        (void) reply;
        assert(reply->type == CmdType::CALL_RMC);
        assert(reply->reply.call.status == 0);
    }

    return 0;
}

int HostClient::do_load(float load, std::vector<uint32_t> &rtts, uint32_t num_reqs)
{
    assert(rmccready);

    uint32_t rtt_idx = 0;
    uint32_t current_max = 0;
    uint32_t late = 0;
    const uint64_t wait_in_nsec = load * 1000;
    const auto maxinflight = get_max_inflight();
    std::queue<time_point> start_times;

    LOG("will issue " << num_reqs << " requests every " << wait_in_nsec << " nanoseconds");
    constexpr auto noop = [](size_t) -> void {};

    for (auto i = 0u; i < maxinflight; i++)
        arm_call_req(get_req(i));

    time_point next_send = time_start();
    for (auto i = 0u; i < num_reqs; i++) {
        next_send += std::chrono::nanoseconds(wait_in_nsec);

        if (load_send_request(start_times) > next_send)
            late++;

        current_max = std::max(current_max, this->inflight);
        if (this->inflight > maxinflight) {
            LOG("curr_inflight=" << this->inflight << " maxinflight=" << maxinflight);
            break;
        }

        /* wait */
        do {
            maybe_poll_sends();
            if (this->inflight > 0) {
                uint32_t polled = rclient.poll_atmost(this->inflight, rclient.get_recv_cq(), noop);
                load_handle_reps(start_times, rtts, polled, rtt_idx);
            }
        } while (time_start() < next_send);
    }

    maybe_poll_sends();
    if (this->inflight > 0) {
        uint32_t polled = rclient.poll_atleast(this->inflight, rclient.get_recv_cq(), noop);
        load_handle_reps(start_times, rtts, polled, rtt_idx);
    }

    LOG("max concurrent=" << current_max);
    LOG("late=" << late);
    return 0;
}

time_point HostClient::load_send_request(std::queue<time_point> &start_times)
{
    time_point submitted;

    post_recv_reply(get_reply(this->req_idx));
    submitted = time_start();
    post_send_req_unsig(get_req(this->req_idx));
    start_times.push(submitted);
    this->inflight++;
    inc_with_wraparound(this->req_idx, get_max_inflight());

    return submitted;
}

void HostClient::load_handle_reps(std::queue<time_point> &start_times,
                                  std::vector<uint32_t> &rtts, uint32_t polled,
                                  uint32_t &rtt_idx)
{
    if (polled == 0)
        return;

    time_point end = time_start();

    for (auto reply = 0u; reply < polled; ++reply) {
        rtts[rtt_idx++] = time_end(start_times.front(), end);
        start_times.pop();
    }

    this->inflight -= polled;
}

void HostClient::last_cmd()
{
    assert(rmccready);

    CmdRequest *req = get_req(0);
    req->type = CmdType::LAST_CMD;
    post_send_req(req);
    rclient.poll_exactly(1, rclient.get_send_cq());
    disconnect();
}

void HostClient::disconnect()
{
    assert(rmccready);
    rclient.disconnect_all();
    rmccready = false;
}

double get_avg(std::vector<long long> &durations)
{
    double avg = 0;
    long long sum = 1;

    for (long long &d: durations)
        sum += d;

    avg = sum / (double) durations.size();
    return avg;
}

double get_median(const std::vector<long long> &durations)
{
    double median = 0;
    size_t vecsize = 0;

    vecsize = durations.size();
    if (durations.size() % 2 == 0)
        median = (durations[vecsize / 2] + durations[(vecsize / 2) - 1]) / (double) 2;
    else
        median = durations[vecsize / 2];

    return median;
}

void print_stats_maxinflight(std::vector<long long> &durations, int maxinflight)
{
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

void print_stats_load(std::vector<uint32_t> &durations)
{
    double avg = 0;
    double median = 0;

    unsigned int remove = durations.size() * 0.1;
    auto d2 = std::vector<long long>(durations.begin() + remove, durations.end());
    std::sort(d2.begin(), d2.end());
    avg = get_avg(d2);
    median = get_median(d2);

    std::cout << "avg (skipped first " << remove << ")=" << std::fixed << avg << "\n";
    std::cout << "median (skipped first " << remove << ")=" << median << "\n";

    std::cout << "rtt\n";
    for (long long &d: std::vector<long long>(durations.begin() + remove, durations.end()))
        std::cout << d << "\n";
}

/* keeps maxinflight active requests at all times */
void benchmark_maxinflight(HostClient &client, std::string ofile, int maxinflight)
{
    const char *prog = R"(void hello() { printf("hello world\n"); })";
    RMC rmc(prog);
    std::vector<long long> durations(NUM_REPS);
    long long duration;
    std::ofstream stream(ofile, std::ofstream::out);

    RMCId id = client.get_rmc_id(rmc);
    LOG("got id=" << id);

    // warm up
    LOG("maxinflight: warming up");
    client.do_maxinflight(duration, maxinflight);

    LOG("maxinflight: benchmark start");
    for (size_t rep = 0; rep < NUM_REPS; ++rep) {
        client.do_maxinflight(duration, maxinflight);
        durations[rep] = duration;
    }
    LOG("maxinflight: benchmark end");
    client.last_cmd();

    print_stats_maxinflight(durations, maxinflight);
}

void benchmark_load(HostClient &client, float load)
{
    std::vector<uint32_t> rtts(LOAD_NUM_REQS);
    const uint32_t max = client.get_max_inflight();

    LOG("get_max_inflight()=" << max);


    LOG("load: warming up");
    client.do_load(load, rtts, max);

    LOG("load: benchmark start");
    client.do_load(load, rtts, LOAD_NUM_REQS);
    LOG("load: benchmark end");
    client.last_cmd();

    print_stats_load(rtts);
}

int main(int argc, char* argv[])
{
    cxxopts::Options opts("client", "RMC client");

    opts.add_options()
        ("s,server", "nicserver address", cxxopts::value<std::string>())
        ("p,port", "nicserver port", cxxopts::value<int>()->default_value("30000"))
        ("o,output", "output file", cxxopts::value<std::string>())
        ("mode", "client mode, can be: maxinflight or load", cxxopts::value<std::string>())
        ("m,maxinflight", "max num of reqs in flight (for mode=maxinflight)", cxxopts::value<int>()->default_value("0"))
        ("l,load", "send 1 new req every these many microseconds (for mode=load)", cxxopts::value<float>()->default_value("0.0"))
        ("h,help", "Print usage")
    ;

    std::string server, ofile, mode;
    unsigned int port;
    unsigned int maxinflight = {0};
    float load = {0};

    try {
        auto result = opts.parse(argc, argv);

        if (result.count("help"))
            die(opts.help());

        server = result["server"].as<std::string>();
        port = result["port"].as<int>();
        ofile = result["output"].as<std::string>();
        mode = result["mode"].as<std::string>();
        maxinflight = result["maxinflight"].as<int>();
        load = result["load"].as<float>();

        if (mode != "load" && mode != "maxinflight") {
            std::cerr << "need to specify mode: load or maxinflight\n";
            die(opts.help());
        } else if (mode == "load" && load == 0) {
            die("mode=load requires load > 0");
        } else if (mode == "maxinflight" && maxinflight == 0) {
            die("mode=maxinflight requires maxinflight > 0");
        }
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        die(opts.help());
    }

    HostClient client(RDMAPeer::QP_ATTRS_MAX_OUTSTAND_SEND_WRS, 1);
    client.connect(server, port);

    if (mode == "maxinflight")
        benchmark_maxinflight(client, ofile, maxinflight);
    else
        benchmark_load(client, load);
}
