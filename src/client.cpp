#include <fstream>
#include <algorithm>
#include <unistd.h>
#include "utils/cxxopts.h"
#include "client.h"
#include "rmc.h"
#include "utils/utils.h"
#include "utils/logger.h"

const int NUM_REPS = 100;
const std::vector<int> BUFF_SIZES = {1024, 2048, 4096, 8192, 16384, 32768, 65536, 131072, 262144, 524288};

void HostClient::connect(const std::string &ip, const std::string &port)
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

int HostClient::call_rmc(long long &duration, int maxinflight)
{
    assert(rmccready);

    int curr_inflight = 0;
    int polled = 0;
    int buf_idx = 0;
    int total_reqs = 1000;
    //int sent_reqs = 0;
    //int recv_replies = 0;

    if (maxinflight > (int) RDMAPeer::MAX_UNSIGNALED_SENDS)
        die("max in flight > MAX_UNSIGNALED_SENDS");


    time_point start = time_start();
    for (int i = 0; i < total_reqs; i++) {
        /* send as many requests as we have credits for */
        if (curr_inflight < maxinflight) {
            post_recv_reply(get_reply(buf_idx));
            arm_call_req(get_req(buf_idx));
            post_send_req_unsig(get_req(buf_idx));
            curr_inflight++;
            buf_idx = (buf_idx + 1) % maxinflight;
            //sent_reqs++;
            //LOG("sent_reqs=" << sent_reqs);
        }

        polled = 0;
        if (curr_inflight > 0) {
            if (curr_inflight < maxinflight)
                polled = rclient.poll_atmost(maxinflight, rclient.get_recv_cq());
            else
                polled = rclient.poll_atleast(1, rclient.get_recv_cq());
        }

        curr_inflight -= polled;
        //recv_replies += polled;
        //LOG("recvd replies=" << recv_replies);
    }

    if (curr_inflight > 0)
        rclient.poll_exactly(curr_inflight, rclient.get_recv_cq());
    duration = time_end(start);

    /* check CmdReplys */
    for (int i = 0; i < maxinflight; ++i) {
        CmdReply *reply = get_reply(i);
        (void) reply;
        assert(reply->type == CmdType::CALL_RMC);
        assert(reply->reply.call.status == 0);
    }

    return 0;
}

//int HostClient::call_one_rmc(const RMCId &id, const size_t arg,
//                         long long &duration)
//{
//    assert(rmccready);
//
//    CmdRequest *req = get_req(0);
//    post_recv_reply(get_reply(0));
//    arm_call_req(req, id, arg);
//
//    time_point start = time_start();
//    post_send_req_unsig(req);
//    /* wait to receive 1 replies */
//    rclient.poll_exactly(1, rclient.get_recv_cq());
//    duration = time_end(start);
//
//    /* check CmdReply */
//    CmdReply *reply = get_reply(0);
//    (void) reply;
//    assert(reply->type == CmdType::CALL_RMC);
//    assert(reply->reply.call.status == 0);
//
//    return 0;
//}

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
    rclient.disconnect();
    rmccready = false;
}

void print_stats(std::vector<long long> &durations, int maxinflight)
{
    long long sum = 0;
    double avg = 0;
    double median = 0;
    size_t vecsize = 0;

    vecsize = durations.size();
    std::sort(durations.begin(), durations.end());

    /* avg */
    for (long long &d: durations)
        sum += d;
    avg = sum / (double) vecsize;

    /* median */
    if (durations.size() % 2 == 0)
        median = (durations[vecsize / 2] + durations[(vecsize / 2) - 1]) / (double) 2;
    else
        median = durations[vecsize / 2];

    std::cout << "max inflight=" << maxinflight << "\n";
    std::cout << "avg=" << avg << "\n";
    std::cout << "median=" << median << "\n";
}

void benchmark(std::string server, std::string port, std::string ofile, int maxinflight)
{
    HostClient client(RDMAPeer::MAX_UNSIGNALED_SENDS);
    const char *prog = R"(void hello() { printf("hello world\n"); })";
    RMC rmc(prog);
    std::vector<long long> durations(NUM_REPS);
    long long duration;
    std::ofstream stream(ofile, std::ofstream::out);

    client.connect(server, port);
    RMCId id = client.get_rmc_id(rmc);
    LOG("got id=" << id);

    // warm up
    //const int &bufsize = BUFF_SIZES[0];
    client.call_rmc(duration, maxinflight);

    for (size_t rep = 0; rep < NUM_REPS; ++rep) {
        client.call_rmc(duration, maxinflight);
        durations[rep] = duration;
    }

    print_stats(durations, maxinflight);

    // real thing
    //for (size_t bufidx = 0; bufidx < BUFF_SIZES.size(); ++bufidx) {
    //    const int &bufsize = BUFF_SIZES[bufidx];
    //    for (size_t rep = 0; rep < NUM_REPS; ++rep) {
    //        client.call_rmc(id, bufsize, duration);
    //        durations[rep] = duration;
    //    }

    //    print_durations(stream, bufsize, durations);
    //}

    client.last_cmd();
}

/* sends one req at a time */
//void benchmark_one(std::string server, std::string port, std::string ofile)
//{
//    HostClient client(1);
//    const char *prog = R"(void hello() { printf("hello world\n"); })"; // unused
//    RMC rmc(prog);
//    long long duration;
//    std::vector<long long> durations(NUM_REPS);
//    const int bufsize = 0; // unused
//    std::ofstream stream(ofile, std::ofstream::out);
//
//    client.connect(server, port);
//    RMCId id = client.get_rmc_id(rmc);
//    LOG("got id=" << id);
//
//    /* warm up */
//    for (int i = 0; i < 10; ++i) {
//        client.call_one_rmc(id, bufsize, duration);
//    }
//
//    for (int i = 0; i < NUM_REPS; ++i) {
//        client.call_one_rmc(id, bufsize, duration);
//        durations[i] = duration;
//    }
//
//    client.last_cmd();
//    print_durations(stream, bufsize, durations);
//}

int main(int argc, char* argv[])
{
    cxxopts::Options opts("client", "RMC client");

    opts.add_options()
        ("s,server", "nicserver address", cxxopts::value<std::string>())
        ("p,port", "nicserver port", cxxopts::value<std::string>()->default_value("30000"))
        ("o,output", "output file", cxxopts::value<std::string>())
        ("m,maxinflight", "max num of reqs in flight", cxxopts::value<int>())
        ("h,help", "Print usage")
    ;

    std::string server, port, ofile;
    int maxinflight;

    try {
        auto result = opts.parse(argc, argv);

        if (result.count("help"))
            die(opts.help());

        server = result["server"].as<std::string>();
        port = result["port"].as<std::string>();
        maxinflight = result["maxinflight"].as<int>();
        ofile = result["output"].as<std::string>();
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        die(opts.help());
    }

    benchmark(server, port, ofile, maxinflight);
    //benchmark_one(server, port, ofile);
}
