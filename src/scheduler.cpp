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
    CoroRMC<int> *rmc = new auto(rmc_test(ns.rclient, num_llnodes));
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

void RMCScheduler::dispatch_new_req(CmdRequest *req)
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
    //auto search = id_rmc_map.find(id); unused
    static int req_idx = 0;
    static int reply_idx = 0;
    int new_reqs = 0;

    /* if there's an RMC ready to run, run it */
    while (!runqueue.empty()) {
        CoroRMC<int> *rmc = runqueue.front();
        runqueue.pop();

        /* if RMC is not done running, add it to memqueue.
           if done, send a reply back to client */
        if (!rmc->resume()) {
            memqueue.push(rmc);
        } else {
            CmdReply *reply = ns.get_reply(reply_idx);
            reply->type = CmdType::CALL_RMC;
            ns.post_send_uns_reply(reply);
            reply_idx = (reply_idx + 1) % ns.bsize;
        }
    }

    /* if there are RMCs waiting for their host mem accesses to finish,
       poll their qp */
    if (!memqueue.empty()) {
        int completed = ns.rclient.poll_async(); // not blocking
        for (int i = 0; i < completed; ++i) {
            runqueue.push(memqueue.front());
            memqueue.pop();
        }
    }

    if (!this->recvd_disconnect) {
        new_reqs = ns.rserver.poll_atmost(1, ns.rserver.get_recv_cq());
        if (new_reqs > 0) {
            assert(new_reqs == 1);
            this->dispatch_new_req(ns.get_req(req_idx));
            ns.post_recv_req(ns.get_req(req_idx));
            req_idx = (req_idx + 1) % ns.bsize;
        }
    }
}
