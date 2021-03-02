#include "scheduler.h"
CoroRMC<int> rmc_test(OneSidedClient &client, RMCRequestHandler *request_handler, size_t num_nodes) {
    // getting a local buffer should be explicit here
    // consider cpu locality into the design
    uintptr_t base_addr = (uintptr_t) client.get_remote_base_addr();
    LLNode *node = (LLNode *) co_await client.readfromcoro(request_handler, 0, sizeof(LLNode));

    for (size_t i = 0; i < num_nodes - 1; ++i) {
        uint32_t offset = (uintptr_t) node->next - base_addr;
        node = (LLNode *) co_await client.readfromcoro(request_handler, offset, sizeof(LLNode)); // this should take node->next vaddr
    }
}

RMCScheduler::RMCScheduler(NICServer &nicserver): ns(nicserver) {
    request_handler.mem_queue = &mem_queue;
    request_handler.buffer_queue = &buffer_queue;
    request_handler.run_queue = &run_queue;
    request_handler.client = &ns.rclient;
}

void RMCScheduler::req_new_rmc(CmdRequest *req)
{
    assert(req->type == CmdType::CALL_RMC);
    //CallReply &callreply = reply->reply.call;
    //CallReq &callreq = req->request.call;

    //size_t arg = std::stoull(callreq.data);
    static int id = 0;
    CoroRMC<int> *rmc = new auto(rmc_test(ns.rclient, &request_handler, num_llnodes));
    rmc->id = id++;
    run_queue.push(rmc);
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

    if (!buffer_queue.empty() && request_handler.is_mem_ready()) {
        while (!buffer_queue.empty() && request_handler.is_mem_ready()) {
            auto rmc_req = buffer_queue.front();
            buffer_queue.pop();

            request_handler.start_rmc_processing(rmc_req.first);
            request_handler.post(rmc_req.second);
            request_handler.end_rmc_processing(false);
        }
    }

    request_handler.end_batched_ops(); 

    auto run_queue_size = run_queue.size();
    for (size_t i = 0; i < run_queue_size; i++) {
        CoroRMC<int> *rmc = run_queue.front();
        run_queue.pop();

        request_handler.start_rmc_processing(rmc);
        /* if RMC is done running, send a reply back to client */
        
        bool done = false;
        if (rmc->resume()) {
            CmdReply *reply = ns.get_reply(reply_idx);
            ns.post_send_uns_reply(reply);
            reply_idx = (reply_idx + 1) % ns.bsize;
            done = true;
        }

        request_handler.end_rmc_processing(done);
    }

    request_handler.end_batched_ops(); 

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
    if (!mem_queue.empty()) {
        int completed = ns.rclient.poll_reads_atmost(4);

        for (int i = 0; i < completed; ++i) {
            run_queue.push(mem_queue.front());
            mem_queue.pop();
        }
    }

}
