#include "cxxopts.h"
#include "nicserver.h"
#include "utils.h"

void NICClient::connect(const std::string &ip, const std::string &port)
{
    assert(!ncready);
    rclient.connect_to_server(ip, port);

    req_buf_mr = rclient.register_mr(req_buf.get(), sizeof(CmdRequest),
                                    IBV_ACCESS_LOCAL_WRITE);
    rdma_mr = rclient.register_mr(rdma_buffer, HostServer::RDMA_BUFF_SIZE,
                                    IBV_ACCESS_LOCAL_WRITE);

    ncready = true;
    recv_rdma_mr();
}

int RMCWorker::execute(const RMCId &id, CallReply &reply)
{
    uint32_t offset = 0;
    uint32_t size = 16; // TODO: get from rmc args

    rclient.readhost(offset, size);
    std::string res = rdma_buffer_as_str(offset, size);
    size_t hash = std::hash<std::string>{}(res);
    num_to_str<size_t>(hash, reply.data, MAX_RMC_REPLY_LEN);
    return 0;
}

/* Compute the id for this rmc, if it doesn't exist, register it in map.
   Return the id */
void NICServer::req_get_rmc_id()
{
    assert(nsready);
    assert(req_buf->type == CmdType::GET_RMCID);

    RMC rmc(req_buf->request.getid.rmc);
    RMCId id = sched.get_rmc_id(rmc);

    /* get_id reply */
    reply_buf->type = CmdType::GET_RMCID;
    reply_buf->reply.getid.id = id;
    post_send_reply();
    rserver.blocking_poll_nofunc(1);
}

void NICServer::req_call_rmc()
{
    assert(nsready);
    assert(req_buf->type == CmdType::CALL_RMC);
    CallReply reply;

    RMCId id = req_buf->request.call.id;
    sched.call_rmc(id, reply);

    reply_buf->type = CmdType::CALL_RMC;
    memcpy(&reply_buf->reply.call, &reply, sizeof(reply));
    //reply_buf->reply.call.status = status;
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
            req_get_rmc_id();
            break;
        case CmdType::CALL_RMC:
            req_call_rmc();
            break;
        case CmdType::LAST_CMD:
            disconnect();
            break;
        default:
            die("unrecognized CmdRequest type\n");
        }
    }

    LOG("handled one request");
}

void NICServer::disconnect()
{
    assert(nsready);

    LOG("received disconnect req");
    rserver.disconnect_events();
    nsready = false;
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

    NICClient nicclient;
    RMCScheduler sched(nicclient);
    NICServer nicserver(sched);

    LOG("connecting to hostserver.");
    nicclient.connect(hostaddr, hostport);

    LOG("waiting for hostclient to connect.");
    nicserver.connect(std::stoi(clientport));
    nicserver.handle_requests();
}
