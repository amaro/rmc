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

const int NUM_REPS = 1;
const std::vector<int> BUFF_SIZES = {1024, 2048, 4096, 8192, 16384, 32768, 65536, 131072, 262144, 524288};
const unsigned int LOAD_NUM_REQS = 1000000;

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

int HostClient::do_load(float load, std::vector<long long> &rtts, unsigned int num_reqs)
{
    assert(rmccready);

    unsigned int polled = 0;
    unsigned int curr_inflight = 0;
    unsigned int buf_idx = 0;
    unsigned int rtt_idx = 0;
    unsigned int maxinflight = RDMAPeer::QP_ATTRS_MAX_OUTSTAND_SEND_WRS - 1;
    unsigned int max_concurrent = 0;
    long long wait_in_nsec = load * 1000;
    std::queue<time_point> start_times;

    LOG("will issue " << num_reqs << " requests every " << wait_in_nsec << " nanoseconds");
    auto noop = [](size_t) -> void {};
    auto handle_replies = [&] () -> void {
        for (auto reply = 0u; reply < polled; ++reply) {
            rtts[rtt_idx++] = time_end(start_times.front());
            start_times.pop();
        }

        curr_inflight -= polled;
    };

    time_point next_send = time_start();
    for (auto i = 0u; i < num_reqs; i++) {
        next_send += std::chrono::nanoseconds(wait_in_nsec);

        post_recv_reply(get_reply(buf_idx));
        arm_call_req(get_req(buf_idx));
        start_times.push(time_start());
        post_send_req_unsig(get_req(buf_idx));
        curr_inflight++;
        buf_idx = (buf_idx + 1) % maxinflight;

        max_concurrent = std::max(max_concurrent, curr_inflight);
        if (curr_inflight > maxinflight) {
            LOG("curr_inflight=" << curr_inflight << " maxinflight=" << maxinflight);
            break;
        }

        /* wait */
        do {
            maybe_poll_sends();
            polled = rclient.poll_atmost(curr_inflight, rclient.get_recv_cq(), noop);
            handle_replies();
        } while (time_start() < next_send);
    }

    if (curr_inflight > 0) {
        polled = rclient.poll_atleast(curr_inflight, rclient.get_recv_cq(), noop);
        handle_replies();
    }

    LOG("max concurrent=" << max_concurrent);
    return 0;
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

void print_stats_load(std::vector<long long> &durations)
{
    double avg = 0;
    double median = 0;

    auto d2 = std::vector<long long>(durations.begin() + 100000, durations.end());
    std::sort(d2.begin(), d2.end());
    avg = get_avg(d2);
    median = get_median(d2);

    std::cout << "avg (skipped first 100k)=" << std::fixed << avg << "\n";
    std::cout << "median (skipped first 100k)=" << median << "\n";
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
    std::vector<long long> rtts(LOAD_NUM_REQS);
    LOG("load: warming up");
    client.do_load(load, rtts, 100);

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
