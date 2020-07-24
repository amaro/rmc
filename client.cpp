#include "cxxopts.h"
#include "client.h"
#include "rmc.h"

/* post a recv for CmdReply */
void Client::post_recv_reply()
{
    assert(rmccready);

    struct ibv_sge sge = {
        .addr = (uintptr_t) reply_buf.get(),
        .length = sizeof(*(reply_buf.get())),
        .lkey = reply_buf_mr->lkey
    };

    rclient.post_simple_recv(&sge);
}

void Client::post_send_req()
{
    assert(rmccready);

    struct ibv_sge sge = {
        .addr = (uintptr_t) req_buf.get(),
        .length = sizeof(CmdRequest),
        .lkey = req_buf_mr->lkey
    };

    rclient.post_simple_send(&sge);
}

void Client::connect(const std::string &ip, const std::string &port)
{
    assert(!rmccready);
    rclient.connect_to_server(ip, port);

    req_buf_mr = rclient.register_mr(req_buf.get(), sizeof(CmdRequest), 0);
    reply_buf_mr = rclient.register_mr(reply_buf.get(), sizeof(CmdReply), IBV_ACCESS_LOCAL_WRITE);
    rmccready = true;
}

RMCId Client::get_rmc_id(const RMC &rmc)
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

int Client::call_rmc(const RMCId &id)
{
    assert(rmccready);

    post_recv_reply();

    /* call request */
    req_buf->type = CmdType::CALL_RMC;
    req_buf->request.call.id = id;
    post_send_req();

    /* poll twice, one for send, one for recv */
    rclient.blocking_poll_nofunc(2);

    /* read CmdReply */
    assert(reply_buf->type == CmdType::CALL_RMC);
    return reply_buf->reply.call.status;
}

void Client::last_cmd()
{
    assert(rmccready);

    req_buf->type = CmdType::LAST_CMD;
    post_send_req();

    /* poll once for send */
    rclient.blocking_poll_nofunc(1);

    disconnect();
}

void Client::disconnect()
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
    Client client;

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

    client.connect(server, port);

    const char *prog = R"(void hello() { printf("hello world\n"); })";

    RMC rmc(prog);
    RMCId id = client.get_rmc_id(rmc);
    std::cout << "got id=" << id << "\n";
    client.call_rmc(id);
    client.last_cmd();
}
