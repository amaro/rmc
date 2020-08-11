#include <fstream>
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

    req_buf_mr = rclient.register_mr(req_buf.get(), sizeof(CmdRequest), 0);
    reply_buf_mr = rclient.register_mr(reply_buf.get(), sizeof(CmdReply), IBV_ACCESS_LOCAL_WRITE);
    rmccready = true;
}

RMCId HostClient::get_rmc_id(const RMC &rmc)
{
    assert(rmccready);

    post_recv_reply();

    /* get_id request */
    req_buf->type = CmdType::GET_RMCID;
    rmc.copy(req_buf->request.getid.rmc, sizeof(req_buf->request.getid.rmc));
    post_send_req(false);

    rclient.poll_exactly(1, rclient.get_recv_cq());

    /* read CmdReply */
    assert(reply_buf->type == CmdType::GET_RMCID);
    return reply_buf->reply.getid.id;
}

int HostClient::call_rmc(const RMCId &id, const size_t arg,
                         long long &duration)
{
    assert(rmccready);

    post_recv_reply();

    /* call request */
    arm_call_req(id, arg);

    time_point start = time_start();
    post_send_req(false);
    rclient.poll_exactly(1, rclient.get_recv_cq());
    duration = time_end(start);

    /* read CmdReply */
    assert(reply_buf->type == CmdType::CALL_RMC);
    return reply_buf->reply.call.status;
}

void HostClient::last_cmd()
{
    assert(rmccready);

    req_buf->type = CmdType::LAST_CMD;
    post_send_req(true);

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
    HostClient client;
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
        assert(!client.call_rmc(id, bufsize, duration));

    // real thing
    for (size_t bufidx = 0; bufidx < BUFF_SIZES.size(); ++bufidx) {
        const int &bufsize = BUFF_SIZES[bufidx];
        for (size_t rep = 0; rep < NUM_REPS; ++rep) {
            assert(!client.call_rmc(id, bufsize, duration));
            durations[rep] = duration;
        }

        print_durations(stream, bufsize, durations);
    }

    client.last_cmd();
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
}
