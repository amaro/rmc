#include "utils/cxxopts.h"
#include "nicserver.h"
#include "scheduler.h"
#include "allocator.h"
#include "utils/utils.h"

void NICServer::connect(const unsigned int &port)
{
    assert(!nsready);
    rserver.connect_from_client(port);

    /* nic writes incoming requests */
    req_buf_mr = rserver.register_mr(&req_buf[0], sizeof(CmdRequest)*bsize, IBV_ACCESS_LOCAL_WRITE);
    /* cpu writes outgoing replies */
    reply_buf_mr = rserver.register_mr(&reply_buf[0], sizeof(CmdReply)*bsize, 0);

    nsready = true;
}

/* not inline, but datapath inside is inline */

void NICServer::init(RMCScheduler &sched)
{
    assert(nsready);

    /* handle the initial rmc get id call */
    post_batched_recv_req(rserver.get_ctrl_ctx(), 0, 1);
    rserver.poll_exactly(1, rserver.get_recv_cq());
    sched.dispatch_new_req(get_req(0));

    /* post the initial recvs */
    post_batched_recv_req(rserver.get_ctrl_ctx(), 0, bsize);

    sched.debug_allocate();
    sched.run();
    sched.debug_print_stats();
}

void NICServer::disconnect()
{
    assert(nsready);

    LOG("received disconnect req");
    rserver.disconnect_events();
    nsready = false;
}

void NICServer::start(RMCScheduler &sched, const std::string &hostaddr,
                        const unsigned int &hostport, const unsigned int &clientport)
{
    LOG("connecting to hostserver.");
    onesidedclient.connect(hostaddr, hostport);

    LOG("waiting for hostclient to connect.");
    this->connect(clientport);
    this->init(sched);
}

void NICServer::post_batched_recv_req(RDMAContext &ctx, unsigned int startidx,
                                      unsigned int num_reqs)
{
    assert(nsready);

    unsigned int max_batch_size = RDMAContext::MAX_BATCHED_RECVS;
    unsigned int num_batches = num_reqs / max_batch_size;
    unsigned int batch_size = 0;

    if (num_reqs % max_batch_size != 0)
        num_batches++;

    for (auto batch = 0u; batch < num_batches; ++batch) {
        if (num_reqs > (batch + 1) * max_batch_size)
            batch_size = max_batch_size;
        else
            batch_size = num_reqs - (batch * max_batch_size);

        ctx.post_batched_recv(get_req(startidx + batch * max_batch_size),
                              sizeof(CmdRequest), batch_size, req_buf_mr->lkey);
    }
}

int main(int argc, char* argv[])
{
    cxxopts::Options opts("nicserver", "NIC Server");

    opts.add_options()
        ("hostaddr", "Host server address to connect to", cxxopts::value<std::string>())
        ("hostport", "Host server port", cxxopts::value<int>()->default_value("30001"))
        ("numqps", "Number of RC qps to hostserver", cxxopts::value<int>())
        ("clientport", "Host client port to listen to", cxxopts::value<int>()->default_value("30000"))
        ("llnodes", "Number of linked list nodes to traverse", cxxopts::value<int>()->default_value("8"))
        ("h,help", "Print usage")
    ;

    std::string hostaddr;
    unsigned int hostport, clientport, llnodes, numqps;

    try {
        auto result = opts.parse(argc, argv);

        if (result.count("help"))
            die(opts.help());

        hostaddr = result["hostaddr"].as<std::string>();
        hostport = result["hostport"].as<int>();
        numqps = result["numqps"].as<int>();
        clientport = result["clientport"].as<int>();
        llnodes = result["llnodes"].as<int>();

        auto max_nodes = HostServer::RDMA_BUFF_SIZE / sizeof(struct LLNode);
        if (llnodes > max_nodes)
            throw std::runtime_error("llnodes > max_nodes");

    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        die(opts.help());
    }

    OneSidedClient onesidedclient(numqps);
    RDMAServer rserver(1);
    NICServer nicserver(onesidedclient, rserver, RDMAPeer::QP_ATTRS_MAX_OUTSTAND_SEND_WRS);

    RMCAllocator::init();
    RMCScheduler sched(nicserver);
    sched.set_num_llnodes(llnodes);

    nicserver.start(sched, hostaddr, hostport, clientport);
    RMCAllocator::release();
    LOG("bye.");
}
