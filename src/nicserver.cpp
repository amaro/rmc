#include "utils/cxxopts.h"
#include "nicserver.h"
#include "utils/utils.h"

void NICServer::connect(int port)
{
    assert(!nsready);
    rserver.connect_events(port);

    /* nic writes incoming requests */
    req_buf_mr = rserver.register_mr(&req_buf[0], sizeof(CmdRequest)*bsize, IBV_ACCESS_LOCAL_WRITE);
    /* cpu writes outgoing replies */
    reply_buf_mr = rserver.register_mr(&reply_buf[0], sizeof(CmdReply)*bsize, 0);

    nsready = true;
}

/* not inline, but datapath inside is inline */
void NICServer::dispatch(CmdRequest *req, CmdReply *reply)
{
    switch (req->type) {
    case CmdType::GET_RMCID:
        return req_get_rmc_id(req, reply);
    case CmdType::CALL_RMC:
        return req_call_rmc(req, reply);
    case CmdType::LAST_CMD:
        return disconnect();
    default:
        DIE("unrecognized CmdRequest type");
    }
}

void NICServer::handle_requests()
{
    assert(nsready);

    /* handle the rmc get id call */
    post_recv_req(get_req(0));
    rserver.poll_exactly(1, rserver.get_recv_cq());
    dispatch(get_req(0), get_reply(0));

    for (size_t i = 0; i < bsize; ++i)
        post_recv_req(get_req(i));

    while (nsready) {
        /* wait for recvs to arrive in-order */
        for (size_t i = 0; i < bsize; ++i) {
            rserver.poll_exactly(1, rserver.get_recv_cq()); // poll for a recv'ed req
            dispatch(get_req(i), get_reply(i));

            if (!nsready)
                return;
        }

        /* post receives of next batch */
        for (size_t i = 0; i < bsize; ++i)
            post_recv_req(get_req(i));
    }
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
        ("llnodes", "Number of linked list nodes to traverse", cxxopts::value<int>()->default_value("65536"))
        ("h,help", "Print usage")
    ;

    std::string hostaddr, hostport, clientport;
    unsigned int llnodes;

    try {
        auto result = opts.parse(argc, argv);

        if (result.count("help"))
            die(opts.help());

        hostaddr = result["hostaddr"].as<std::string>();
        hostport = result["hostport"].as<std::string>();
        clientport = result["clientport"].as<std::string>();
        llnodes = result["llnodes"].as<int>();

        auto max_nodes = HostServer::RDMA_BUFF_SIZE / sizeof(struct LLNode);
        if (llnodes > max_nodes)
            throw std::runtime_error("llnodes > max_nodes");

    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        die(opts.help());
    }

    OneSidedClient nicclient;
    RMCScheduler sched(nicclient);
    sched.set_num_llnodes(llnodes);
    NICServer nicserver(sched, RDMAPeer::MAX_UNSIGNALED_SENDS);

    LOG("connecting to hostserver.");
    nicclient.connect(hostaddr, hostport);

    LOG("waiting for hostclient to connect.");
    nicserver.connect(std::stoi(clientport));
    nicserver.handle_requests();
}
