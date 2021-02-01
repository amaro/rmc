#include <fstream>
#include <unistd.h>
#include "utils/cxxopts.h"
#include "client.h"
#include "rmc.h"
#include "utils/utils.h"
#include "utils/logger.h"

const int NUM_REPS = 1;
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

int HostClient::call_rmc(const RMCId &id, const size_t arg,
                         long long &duration)
{
    assert(rmccready);

    int max_inflight = bsize;
    int curr_inflight = 0;
    int polled = 0;
    int buf_idx = 0;
    int total_reqs = 1000;

    LOG("batch size=" << bsize);
    time_point start = time_start();
    for (int i = 0; i < total_reqs; i++) {
        /* send as many requests as we have credits for */
        if (curr_inflight < max_inflight) {
            post_recv_reply(get_reply(buf_idx));
            arm_call_req(get_req(buf_idx), id, arg);
            post_send_req_unsig(get_req(buf_idx));
            curr_inflight++;
            buf_idx = (buf_idx + 1) % bsize;
        }

        polled = 0;
        if (curr_inflight > 0) {
            if (curr_inflight < max_inflight)
                polled = rclient.poll_atmost(bsize, rclient.get_recv_cq());
            else
                polled = rclient.poll_atleast(1, rclient.get_recv_cq());
        }

        curr_inflight -= polled;
    }

    rclient.poll_exactly(curr_inflight, rclient.get_recv_cq());
    duration = time_end(start);
    std::cout << "duration=" << duration << " ns\n";

    /* check CmdReplys */
    for (size_t i = 0; i < bsize; ++i) {
        CmdReply *reply = get_reply(i);
        (void) reply;
        assert(reply->type == CmdType::CALL_RMC);
        assert(reply->reply.call.status == 0);
    }

    return 0;
}

int HostClient::call_one_rmc(const RMCId &id, const size_t arg,
                         long long &duration)
{
    assert(rmccready);

    CmdRequest *req = get_req(0);
    post_recv_reply(get_reply(0));
    arm_call_req(req, id, arg);

    time_point start = time_start();
    post_send_req_unsig(req);
    /* wait to receive 1 replies */
    rclient.poll_exactly(1, rclient.get_recv_cq());
    duration = time_end(start);

    /* check CmdReply */
    CmdReply *reply = get_reply(0);
    (void) reply;
    assert(reply->type == CmdType::CALL_RMC);
    assert(reply->reply.call.status == 0);

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
    rclient.disconnect();
    rmccready = false;
}

void print_durations(std::ofstream &stream, int bufsize, const std::vector<long long> &durations)
{
    stream << "bufsize=" << bufsize << "\n";
    for (const long long &d: durations)
        stream << d << "\n";
}

void benchmark(std::string server, std::string port, std::string ofile)
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
    const int &bufsize = BUFF_SIZES[0];
    for (size_t rep = 0; rep < NUM_REPS; ++rep)
        client.call_rmc(id, bufsize, duration);

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
void benchmark_one(std::string server, std::string port, std::string ofile)
{
    HostClient client(1);
    const char *prog = R"(void hello() { printf("hello world\n"); })"; // unused
    RMC rmc(prog);
    long long duration;
    std::vector<long long> durations(NUM_REPS);
    const int bufsize = 0; // unused
    std::ofstream stream(ofile, std::ofstream::out);

    client.connect(server, port);
    RMCId id = client.get_rmc_id(rmc);
    LOG("got id=" << id);

    /* warm up */
    for (int i = 0; i < 10; ++i) {
        client.call_one_rmc(id, bufsize, duration);
    }

    for (int i = 0; i < NUM_REPS; ++i) {
        client.call_one_rmc(id, bufsize, duration);
        durations[i] = duration;
    }

    client.last_cmd();
    print_durations(stream, bufsize, durations);
}

int main(int argc, char* argv[])
{
    cxxopts::Options opts("client", "RMC client");

    opts.add_options()
        ("s,server", "nicserver address", cxxopts::value<std::string>())
        ("p,port", "nicserver port", cxxopts::value<std::string>()->default_value("30000"))
        ("o,output", "output file", cxxopts::value<std::string>())
        ("h,help", "Print usage")
    ;

    std::string server, port, ofile;

    try {
        auto result = opts.parse(argc, argv);

        if (result.count("help"))
            die(opts.help());

        server = result["server"].as<std::string>();
        port = result["port"].as<std::string>();
        ofile = result["output"].as<std::string>();
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        die(opts.help());
    }

    benchmark(server, port, ofile);
    //benchmark_one(server, port, ofile);
}
