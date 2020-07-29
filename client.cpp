#include "cxxopts.h"
#include "client.h"
#include "rmc.h"
#include "utils.h"
#include "logger.h"

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
    post_send_req();

    /* poll twice, one for send, one for recv */
    rclient.blocking_poll_nofunc(2);

    /* read CmdReply */
    assert(reply_buf->type == CmdType::GET_RMCID);
    return reply_buf->reply.getid.id;
}

int HostClient::call_rmc(const RMCId &id)
{
    assert(rmccready);

    post_recv_reply();

    /* call request */
    req_buf->type = CmdType::CALL_RMC;
    req_buf->request.call.id = id;

    time_point start = time_start();
    post_send_req();

    /* poll twice, one for send, one for recv */
    rclient.blocking_poll_nofunc(2);
    long long duration = time_end(start);
    LOG("full rmc duration=" << duration << " ns");

    /* read CmdReply */
    assert(reply_buf->type == CmdType::CALL_RMC);
    return reply_buf->reply.call.status;
}

void HostClient::last_cmd()
{
    assert(rmccready);

    req_buf->type = CmdType::LAST_CMD;
    post_send_req();

    /* poll once for send */
    rclient.blocking_poll_nofunc(1);

    disconnect();
}

void HostClient::disconnect()
{
    assert(rmccready);
    rclient.disconnect();
    rmccready = false;
}

int main(int argc, char* argv[])
{
    cxxopts::Options opts("client", "Client for RDMA benchmarks");

    opts.add_options()
        ("s,server", "Server address", cxxopts::value<std::string>())
        ("p,port", "Server port", cxxopts::value<std::string>()->default_value("30000"))
        ("h,help", "Print usage")
    ;

    std::string server;
    std::string port;

    try {
        auto result = opts.parse(argc, argv);

        if (result.count("help"))
            die(opts.help());

        server = result["server"].as<std::string>();
        port = result["port"].as<std::string>();
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        die(opts.help());
    }

    HostClient client;
    client.connect(server, port);

    const char *prog = R"(void hello() { printf("hello world\n"); })";

    RMC rmc(prog);
    RMCId id = client.get_rmc_id(rmc);
    LOG("got id=" << id);
    client.call_rmc(id);
    client.last_cmd();
}
