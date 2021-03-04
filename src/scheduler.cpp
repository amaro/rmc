#include "scheduler.h"
#include "nicserver.h"

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

RMCScheduler::RMCScheduler(NICServer &nicserver) : ns(nicserver) {
    ns.rclient.bufferqueue = &bufferqueue;
    ns.rclient.memqueue = &memqueue;
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

    bool start_batch = false;
    if (!bufferqueue.empty() && memqueue.size() - bufferqueue.size() < RDMAPeer::MAX_QP_INFLIGHT_READS) {
        ns.rclient.start_batched_ops();
        start_batch = true;

        while (!bufferqueue.empty() && memqueue.size() - bufferqueue.size() < RDMAPeer::MAX_QP_INFLIGHT_READS) {
            auto offset_len_pair = bufferqueue.front();
            bufferqueue.pop();
            ns.rclient.post_read(offset_len_pair.first, offset_len_pair.second);
        }
    
        if (memqueue.size() - bufferqueue.size() >= RDMAPeer::MAX_QP_INFLIGHT_READS) {
            start_batch = false;
            ns.rclient.end_batched_ops();
        }
    }

    if (!start_batch && memqueue.size() - bufferqueue.size() < RDMAPeer::MAX_QP_INFLIGHT_READS) {
        start_batch = true;
        ns.rclient.start_batched_ops();
    }

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
            ns.post_send_uns_reply(reply);
            reply_idx = (reply_idx + 1) % ns.bsize;
        }
    }

    if (start_batch)
        ns.rclient.end_batched_ops();

    if (!this->recvd_disconnect) {
        int new_reqs = ns.rserver.poll_atmost(4, ns.rserver.get_recv_cq());
        for (int i = 0; i < new_reqs; ++i) {
            this->dispatch_new_req(ns.get_req(req_idx));
            ns.post_recv_req(ns.get_req(req_idx));
            req_idx = (req_idx + 1) % ns.bsize;
        }
    }

    /* if there are RMCs waiting for their host mem accesses to finish,
       poll their qp */
    if (!memqueue.empty()) {
        int completed = ns.rclient.poll_reads_atmost(4);
        for (int i = 0; i < completed; ++i) {
            runqueue.push(memqueue.front());
            memqueue.pop();
        }
    }
}
