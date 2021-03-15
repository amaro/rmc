#include "scheduler.h"

CoroRMC<int> rmc_test(OneSidedClient &client, size_t num_nodes) {
    // getting a local buffer should be explicit here
    // consider cpu locality into the design
    uintptr_t base_addr = (uintptr_t) client.get_remote_base_addr();
    LLNode *node = (LLNode *) co_await client.readfromcoro(0, sizeof(LLNode));

    for (size_t i = 0; i < num_nodes - 1; ++i) {
        uint32_t offset = (uintptr_t) node->next - base_addr;
        node = (LLNode *) co_await client.readfromcoro(offset, sizeof(LLNode)); // this should take node->next vaddr
    }
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
    runqueue.push(rmc);
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

void RMCScheduler::run()
{
    while (ns.nsready) {
        schedule();

        if (this->recvd_disconnect && !this->executing())
            return ns.disconnect();
    }
}

void RMCScheduler::schedule()
{
    RDMAContext &ctx = get_next_context();
    ns.onesidedclient.start_batched_ops(&ctx);
    /* if there's an RMC ready to run, run it */
    while (!runqueue.empty() && ctx.memqueue.size() < RDMAPeer::MAX_QP_INFLIGHT_READS) {
        CoroRMC<int> *rmc = runqueue.front();
        runqueue.pop();

        /* if RMC is not done running, add it to memqueue.
           if done, send a reply back to client */
        if (!rmc->resume())
            ctx.memqueue.push(rmc);
        else
            reply_client();
    }
    ns.onesidedclient.end_batched_ops();

    check_new_reqs_client();
    poll_comps_host();
}
