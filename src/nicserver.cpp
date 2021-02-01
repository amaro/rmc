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
void NICServer::dispatch_new_req(CmdRequest *req)
{
    switch (req->type) {
    case CmdType::GET_RMCID:
        return req_get_rmc_id(req);
    case CmdType::CALL_RMC:
        return req_new_rmc(req);
    case CmdType::LAST_CMD:
        std::cout << "received disconnect req\n";
        this->recvd_disconnect = true;
        return;
    default:
        DIE("unrecognized CmdRequest type");
    }
}

void NICServer::handle_requests()
{
    assert(nsready);

    /* handle the initial rmc get id call */
    post_recv_req(get_req(0));
    rserver.poll_exactly(1, rserver.get_recv_cq());
    dispatch_new_req(get_req(0));

    for (size_t i = 0; i < bsize; ++i)
        post_recv_req(get_req(i));

    int req_idx = 0;
    int reply_idx = 0;
    int new_reqs = 0;
    while (nsready) {
        if (!this->recvd_disconnect) {
            new_reqs = rserver.poll_atmost(1, rserver.get_recv_cq());
            if (new_reqs > 0) {
                assert(new_reqs == 1);
                dispatch_new_req(get_req(req_idx));
                post_recv_req(get_req(req_idx));
                req_idx = (req_idx + 1) % bsize;
            }
        }

        /* if a task finished, send its reply */
        if (sched.schedule()) {
            CmdReply *reply = get_reply(reply_idx);
            reply->type = CmdType::CALL_RMC;
            post_send_uns_reply(reply);
            reply_idx = (reply_idx + 1) % bsize;
        }

        if (this->recvd_disconnect && !sched.executing())
            return this->disconnect();
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
        ("llnodes", "Number of linked list nodes to traverse", cxxopts::value<int>()->default_value("8"))
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
