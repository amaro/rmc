#ifndef SCHEDULER_H
#define SCHEDULER_H

// #define PERF_STATS

#include <cassert>
#include <cstdlib>
#include <iostream>
#include <unordered_map>
#include <queue>
#include <vector>
#include <rdma/rdma_cma.h>

#include <inttypes.h>

#include "corormc.h"
#include "rmc.h"
#include "nicserver.h"

class NICServer;

/* one RMCScheduler per NIC core */
class RMCScheduler {
    NICServer &ns;

    std::unordered_map<RMCId, RMC> id_rmc_map;
    /* RMCs ready to be run */
    std::queue<CoroRMC<int>*> runqueue;
    /* RMCs waiting for host memory accesses */

    size_t num_llnodes;
    uint16_t num_qps;
    unsigned int req_idx;
    uint32_t reply_idx;
    bool pending_replies;
    /* true if we received a disconnect req, so we are waiting for rmcs to
       finish executing before disconnecting */
    bool recvd_disconnect;

    RMCId get_rmc_id(const RMC &rmc);
    void req_get_rmc_id(CmdRequest *req);
    void req_new_rmc(CmdRequest *req);
    void add_reply(CoroRMC<int> *rmc, RDMAContext &server_ctx);
    void send_poll_replies(RDMAContext &server_ctx);
    void check_new_reqs_client(RDMAContext &server_ctx);
    void poll_comps_host();
    bool executing();

#ifdef PERF_STATS
    bool debug_start = false;
    unsigned int debug_replies = 0;
    unsigned int debug_rmcexecs = 0;
    long long debug_cycles_newreqs = 0;
    long long debug_cycles_replies = 0;
    std::vector<long long> debug_vec_cycles;
    std::vector<unsigned int> debug_vec_reqs;
    std::vector<long long> debug_vec_cycles_reqs;
    std::vector<long long> debug_vec_cycles_replies;
    std::vector<unsigned int> debug_vec_rmcexecs;
    std::vector<unsigned int> debug_vec_replies;
    std::vector<unsigned int> debug_vec_hostcomps;
    std::vector<unsigned int> debug_vec_memqsize;
#endif

public:
    static constexpr int MAX_NEW_REQS_PER_ITER = 16;
    static constexpr int MAX_HOST_COMPS_ITER = 16;
    static constexpr int DEBUG_VEC_RESERVE = 1000000;

    RMCScheduler(NICServer &nicserver) :
        ns(nicserver), num_qps(0), req_idx(0), reply_idx(0), pending_replies(0),
        recvd_disconnect(false) { }

    /* RMC entry points */

    void run();
    void schedule(RDMAClient &rclient);
    void set_num_llnodes(size_t num_nodes);
    void set_num_qps(uint16_t num_qps);
    void dispatch_new_req(CmdRequest *req);

    RDMAContext &get_server_context();
    RDMAContext &get_client_context(unsigned int id);
    RDMAContext &get_next_client_context();

    void debug_capture_stats();
    void debug_allocate();
    void debug_print_stats();
};

inline RMCId RMCScheduler::get_rmc_id(const RMC &rmc)
{
    RMCId id = std::hash<RMC>{}(rmc);

    if (id_rmc_map.find(id) == id_rmc_map.end()) {
        id_rmc_map.insert({id, rmc});
        LOG("registered new id=" << id << "for rmc=" << rmc);
    }

    return id;
}

inline void RMCScheduler::set_num_llnodes(size_t num_nodes)
{
    LOG("num nodes in each linked list=" << num_nodes);
    num_llnodes = num_nodes;
}

inline void RMCScheduler::set_num_qps(uint16_t num_qps)
{
    this->num_qps = num_qps;
}

inline RDMAContext &RMCScheduler::get_server_context()
{
    return ns.rserver.get_ctrl_ctx();
}

inline RDMAContext &RMCScheduler::get_client_context(unsigned int id)
{
    return ns.onesidedclient.get_rclient().get_contexts()[id];
}

inline RDMAContext &RMCScheduler::get_next_client_context()
{
    static uint16_t id = 0;

    RDMAContext &ctx = get_client_context(id);
    inc_with_wraparound(id, num_qps);
    return ctx;
}

inline bool RMCScheduler::executing()
{
    if (!runqueue.empty())
        return true;

    for (const auto &ctx: ns.onesidedclient.get_rclient().get_contexts()) {
        if (!ctx.memqueue.empty())
            return true;
    }

    return false;
}

inline void RMCScheduler::dispatch_new_req(CmdRequest *req)
{
    switch (req->type) {
    case CmdType::GET_RMCID:
        return req_get_rmc_id(req);
    case CmdType::CALL_RMC:
        return req_new_rmc(req);
    case CmdType::LAST_CMD:
        this->recvd_disconnect = true;
        return;
    default:
        DIE("unrecognized CmdRequest type");
    }
}

inline void RMCScheduler::add_reply(CoroRMC<int> *rmc, RDMAContext &server_ctx)
{
#ifdef PERF_STATS
    long long cycles = get_cycles();
#endif

    if (!pending_replies)
        ns.rserver.start_batched_ops(&server_ctx);

    pending_replies = true;
    CmdReply *reply = ns.get_reply(this->reply_idx);
    ns.post_batched_send_reply(server_ctx, reply);
    inc_with_wraparound(this->reply_idx, ns.bsize);
    delete rmc;

#ifdef PERF_STATS
    debug_replies++;
    debug_cycles_replies += get_cycles() - cycles;
#endif
}

inline void RMCScheduler::send_poll_replies(RDMAContext &server_ctx)
{
    auto update_out_sends = [&](size_t batch_size) {
        server_ctx.outstanding_sends -= batch_size;
    };

    if (pending_replies) {
        /* finish the batch */
        ns.rserver.end_batched_ops();
        pending_replies = false;
    }

    if (server_ctx.outstanding_sends >= RDMAPeer::QP_ATTRS_MAX_OUTSTAND_SEND_WRS)
        DIE("error: server_ctx.outstanding_sends=" << server_ctx.outstanding_sends);

    /* poll */
    ns.rserver.poll_batched_atmost(1, ns.rserver.get_send_compqueue(), update_out_sends);
}

inline void RMCScheduler::check_new_reqs_client(RDMAContext &server_ctx)
{
    int new_reqs;
    CmdRequest *req;

#ifdef PERF_STATS
    long long cycles = get_cycles();
#endif

    if (this->recvd_disconnect)
        return;

    auto noop = [](size_t) constexpr -> void {};
    new_reqs = ns.rserver.poll_batched_atmost(MAX_NEW_REQS_PER_ITER, ns.rserver.get_recv_compqueue(),
                                        noop);
#ifdef PERF_STATS
    if (!debug_start && new_reqs > 0)
        debug_start = true;
#endif
    auto prev_req_idx = this->req_idx;
    for (auto i = 0; i < new_reqs; ++i) {
        req = ns.get_req(this->req_idx);
        dispatch_new_req(req);
        inc_with_wraparound(this->req_idx, ns.bsize);
    }

    /* if true, it means we've wrapped around the req buffers.
       so issue two calls, one for the remaining of the buffer, and another one
       for the wrapped around reqs */
    if (new_reqs + prev_req_idx > ns.bsize) {
        ns.post_batched_recv_req(server_ctx, prev_req_idx, ns.bsize - prev_req_idx);
        ns.post_batched_recv_req(server_ctx, 0, new_reqs - (ns.bsize - prev_req_idx));
    } else {
        ns.post_batched_recv_req(server_ctx, prev_req_idx, new_reqs);
    }

#ifdef PERF_STATS
    if (debug_start) {
        debug_vec_reqs.push_back(new_reqs);
        debug_cycles_newreqs += get_cycles() - cycles;
    }
#endif
}

inline void RMCScheduler::poll_comps_host()
{
    auto add_to_runqueue = [this](size_t ctx_id) {
        RDMAContext &ctx = this->get_client_context(ctx_id);
        this->runqueue.push(ctx.memqueue.front());
        ctx.memqueue.pop();
    };

    int comp = ns.onesidedclient.poll_reads_atmost(MAX_HOST_COMPS_ITER, add_to_runqueue);
    (void) comp;
#ifdef PERF_STATS
    if (debug_start)
        debug_vec_hostcomps.push_back(comp);
#endif
}

inline void RMCScheduler::debug_capture_stats()
{
#ifdef PERF_STATS
    if (!debug_start)
        return;

    debug_vec_cycles.push_back(get_cycles());
    debug_vec_cycles_reqs.push_back(debug_cycles_newreqs);
    debug_cycles_newreqs = 0;
    debug_vec_cycles_replies.push_back(debug_cycles_replies);
    debug_cycles_replies = 0;
    debug_vec_replies.push_back(debug_replies);
    debug_replies = 0;
    debug_vec_rmcexecs.push_back(debug_rmcexecs);
    debug_rmcexecs = 0;
    unsigned int memq_size = 0;
    for (auto &ctx: ns.onesidedclient.get_rclient().get_contexts())
        memq_size += ctx.memqueue.size();
    debug_vec_memqsize.push_back(memq_size);
#endif
}

#endif
