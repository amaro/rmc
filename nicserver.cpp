#include "cxxopts.h"
#include "nicserver.h"

void NICServer::post_recv_req()
{
    assert(nsready);

    struct ibv_sge sge = {
        .addr = (uintptr_t) req_buf.get(),
        .length = sizeof(*(req_buf.get())),
        .lkey = req_buf_mr->lkey
    };

    rserver.post_simple_recv(&sge);
}

void NICServer::post_send_reply()
{
    assert(nsready);

    struct ibv_sge sge = {
        .addr = (uintptr_t) reply_buf.get(),
        .length = sizeof(CmdReply),
        .lkey = reply_buf_mr->lkey
    };

    rserver.post_simple_send(&sge);
}

/* Compute the id for this rmc, if it doesn't exist, register it in map.
   Return the id */
void NICServer::get_rmc_id()
{
    assert(nsready);
    assert(req_buf->type == CmdType::GET_RMCID);

    RMC rmc(req_buf->request.getid.rmc);
    RMCId id = std::hash<RMC>{}(rmc);

    if (id_rmc_map.find(id) == id_rmc_map.end()) {
        id_rmc_map.insert({id, rmc});
        std::cout << "registered new id=" << id << "for rmc=" << rmc << "\n";
    }

    /* get_id reply */
    reply_buf->type = CmdType::GET_RMCID;
    reply_buf->reply.getid.id = id;
    post_send_reply();
    rserver.blocking_poll_nofunc(1);
}

void NICServer::call_rmc()
{
    assert(nsready);
    assert(req_buf->type == CmdType::CALL_RMC);

    RMCId id = req_buf->request.call.id;
    int status = 0;
    auto search = id_rmc_map.find(id);

    if (search == id_rmc_map.end()) {
        status = EINVAL;
    } else {
        std::cout << "Called RMC: " << search->second << "\n";
    }

    reply_buf->type = CmdType::CALL_RMC;
    reply_buf->reply.call.status = status;
    post_send_reply();
    rserver.blocking_poll_nofunc(1);
}

void NICServer::connect(int port)
{
    assert(!nsready);
    rserver.connect_events(port);

    /* nic writes incoming requests */
    req_buf_mr = rserver.register_mr(req_buf.get(), sizeof(CmdRequest), IBV_ACCESS_LOCAL_WRITE);
    /* cpu writes outgoing replies */
    reply_buf_mr = rserver.register_mr(reply_buf.get(), sizeof(CmdReply), 0);

    nsready = true;
}

void NICServer::handle_requests()
{
    assert(nsready);

    while (nsready) {
        post_recv_req();
        rserver.blocking_poll_nofunc(1);

        switch (req_buf->type) {
        case CmdType::GET_RMCID:
            get_rmc_id();
            break;
        case CmdType::CALL_RMC:
            call_rmc();
            break;
        case CmdType::LAST_CMD:
            disconnect();
            break;
        default:
            die("unrecognized CmdRequest type\n");
        }
    }

    std::cout << "handled request\n";
}

void NICServer::disconnect()
{
    assert(nsready);

    std::cout << "received disconnect req\n";
    rserver.disconnect_events();
    nsready = false;
}

void NICClient::connect(const std::string &ip, const std::string &port)
{
    assert(!ncready);
    rclient.connect_to_server(ip, port);

    ncready = true;
}

int main(int argc, char* argv[])
{
    cxxopts::Options opts("nicserver", "NIC Server");

    opts.add_options()
        ("hostaddr", "Host server address to connect to", cxxopts::value<std::string>())
        ("hostport", "Host server port", cxxopts::value<std::string>()->default_value("30001"))
        ("clientport", "Host client port to listen to", cxxopts::value<std::string>()->default_value("30000"))
        ("h,help", "Print usage")
    ;

    std::string hostaddr, hostport, clientport;

    try {
        auto result = opts.parse(argc, argv);

        if (result.count("help"))
            die(opts.help());

        hostaddr = result["hostaddr"].as<std::string>();
        hostport = result["hostport"].as<std::string>();
        clientport = result["clientport"].as<std::string>();
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        die(opts.help());
    }

    NICClient nicclient; // interact with hostserver on same host
    NICServer nicserver; // interact with nicclient on diff host

    std::cout << "connecting to hostserver.\n";
    nicclient.connect(hostaddr, hostport);

    std::cout << "waiting for hostclient to connect.\n";
    nicserver.connect(std::stoi(clientport));
    nicserver.handle_requests();
}
