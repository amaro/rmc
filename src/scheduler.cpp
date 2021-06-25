#include "scheduler.h"

CoroRMC<int> rmc_test(OneSidedClient &client, size_t num_nodes) {
    // getting a local buffer should be explicit here
    // consider cpu locality into the design
    uintptr_t base_addr = (uintptr_t) client.get_remote_base_addr();
    uint32_t offset = 0;
    //LLNode *node = (LLNode *) co_await client.readfromcoro(0, sizeof(LLNode));
    LLNode *node = nullptr;

    for (size_t i = 0; i < num_nodes; ++i) {
        node = (LLNode *) co_await client.readfromcoro(offset, sizeof(LLNode)); // this should take node->next vaddr
        offset = (uintptr_t) node->next - base_addr;
    }
}

/* Compute the id for this rmc, if it doesn't exist, register it in map.
   Return the id */
void RMCScheduler::req_get_rmc_id(CmdRequest *req)
{
    assert(ns.nsready);
    assert(req->type == CmdType::GET_RMCID);

    RMC rmc(req->request.getid.rmc);
    RMCId id = this->get_rmc_id(rmc);

    /* for now, just use buffer 0 for get rmc id reply */
    CmdReply *reply = ns.get_reply(0);
    reply->type = CmdType::GET_RMCID;
    reply->reply.getid.id = id;
    ns.post_send_reply(reply);
    ns.rserver.poll_exactly(1, ns.rserver.get_send_cq());
}

void RMCScheduler::req_new_rmc(CmdRequest *req)
{
    assert(req->type == CmdType::CALL_RMC);
    //CallReply &callreply = reply->reply.call;
    //CallReq &callreq = req->request.call;

    //size_t arg = std::stoull(callreq.data);
    static int id = 0;
    CoroRMC<int> *rmc = new auto(rmc_test(ns.onesidedclient, num_llnodes));
    rmc->id = id++;
    runqueue.push_back(rmc);
}

void RMCScheduler::run()
{
    RDMAClient &rclient = ns.onesidedclient.get_rclient();

    while (ns.nsready) {
        schedule(rclient);

        if (this->recvd_disconnect && !this->executing())
            return ns.disconnect();

        debug_capture_stats();
    }
}

void RMCScheduler::schedule(RDMAClient &rclient)
{
#ifdef PERF_STATS
    long long exec_start = get_cycles();
#endif

    static RDMAContext &server_ctx = get_server_context();

    for (auto i = 0u; i < num_qps && !runqueue.empty(); ++i) {
        RDMAContext &ctx = get_next_client_context();

        rclient.start_batched_ops(&ctx);
        while (!runqueue.empty() && ctx.memqueue.size() < RDMAPeer::MAX_QP_INFLIGHT_READS) {
            CoroRMC<int> *rmc = runqueue.front();
            runqueue.pop_front();

            if (!rmc->resume())
                ctx.memqueue.push(rmc);
            else
                add_reply(rmc, server_ctx);

#ifdef PERF_STATS
            debug_rmcexecs++;
#endif
        }
        rclient.end_batched_ops();
    }

    send_poll_replies(server_ctx);
#ifdef PERF_STATS
    /* we do it here because add_reply() above computes its own duration
       that must be added to the time spent in send_poll_replies()
       both of them must be substracted from debug_cycles_execs */
    debug_cycles_execs = get_cycles() - exec_start - debug_cycles_replies;
#endif

    check_new_reqs_client(server_ctx);
    poll_comps_host();

#ifdef PERF_STATS
    debug_cycles = get_cycles() - exec_start;
#endif

}

void RMCScheduler::debug_print_stats()
{
#ifdef PERF_STATS
    if (debug_vec_reqs.size () != debug_vec_replies.size()) {
        std::cout << "vec_reqs size=" << debug_vec_reqs.size() << "\n";
        std::cout << "vec_replies size=" << debug_vec_replies.size() << "\n";
    }

    std::cout << "reqs,replies,memq_size,execs,host_comps,cycle,cycle_reqs,cycle_replies,cycle_execs,cycle_hostcomps\n";
    for (auto i = 0u; i < debug_vec_reqs.size(); ++i) {
        std::cout << debug_vec_reqs[i] << "," << debug_vec_replies[i] << ","
            << debug_vec_memqsize[i] << "," << debug_vec_rmcexecs[i] << ","
            << debug_vec_hostcomps[i] << "," << debug_vec_cycles[i] << ","
            << debug_vec_cycles_reqs[i] << "," << debug_vec_cycles_replies[i] << ","
            << debug_vec_cycles_execs[i] << "," << debug_vec_cycles_hostcomps[i] << "\n";
    }

    std::cout << "total reqs=" << std::accumulate(debug_vec_reqs.begin(), debug_vec_reqs.end(), 0) << "\n";
    std::cout << "total replies=" << std::accumulate(debug_vec_replies.begin(), debug_vec_replies.end(), 0) << "\n";
    std::cout << "total execs=" << std::accumulate(debug_vec_rmcexecs.begin(), debug_vec_rmcexecs.end(), 0) << "\n";
#endif
}

void RMCScheduler::debug_allocate()
{
#ifdef PERF_STATS
    debug_vec_cycles.reserve(DEBUG_VEC_RESERVE);
    debug_vec_cycles_reqs.reserve(DEBUG_VEC_RESERVE);
    debug_vec_cycles_replies.reserve(DEBUG_VEC_RESERVE);
    debug_vec_cycles_execs.reserve(DEBUG_VEC_RESERVE);
    debug_vec_cycles_hostcomps.reserve(DEBUG_VEC_RESERVE);

    debug_vec_reqs.reserve(DEBUG_VEC_RESERVE);
    debug_vec_rmcexecs.reserve(DEBUG_VEC_RESERVE);
    debug_vec_replies.reserve(DEBUG_VEC_RESERVE);
    debug_vec_memqsize.reserve(DEBUG_VEC_RESERVE);
#endif
}
