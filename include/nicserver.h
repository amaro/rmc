#ifndef NIC_SERVER_H
#define NIC_SERVER_H

#include <functional>
#include <unordered_map>
#include <cstdint>
#include "rdmaserver.h"
#include "onesidedclient.h"
#include "hostserver.h"
#include "rmc.h"

class RMCWorker {
    OneSidedClient &rclient;
    unsigned id;

public:
    RMCWorker(OneSidedClient &c, unsigned id) : rclient(c), id(id) {
    }

    int execute(const RMCId &id, CallReply &reply, size_t arg);
    std::string rdma_buffer_as_str(uint32_t offset, uint32_t size) const;
};

inline std::string RMCWorker::rdma_buffer_as_str(uint32_t offset, uint32_t size) const
{
    char *result_buffer = rclient.get_rdma_buffer();
    return std::string(result_buffer[offset], size);
}

inline int RMCWorker::execute(const RMCId &id, CallReply &reply, size_t arg)
{
    uint32_t offset = 0;
    size_t size = arg;

    rclient.readhost(offset, size);
    std::string res = rdma_buffer_as_str(offset, size);
    size_t hash = std::hash<std::string>{}(res);
    num_to_str<size_t>(hash, reply.data, MAX_RMC_REPLY_LEN);
    return 0;
}

class RMCScheduler {
    const unsigned NUM_WORKERS = 1;

    std::unordered_map<RMCId, RMC> id_rmc_map;
    std::vector<std::unique_ptr<RMCWorker>> workers;
    OneSidedClient &client;

public:
    RMCScheduler(OneSidedClient &c) : client(c) {
        for (unsigned i = 0; i < NUM_WORKERS; ++i)
            workers.push_back(std::make_unique<RMCWorker>(client, i));
    }

    /* RMC entry points */
    RMCId get_rmc_id(const RMC &rmc);
    int call_rmc(const RMCId &id, CallReply &reply, size_t arg);
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

inline int RMCScheduler::call_rmc(const RMCId &id, CallReply &reply, size_t arg)
{
    auto search = id_rmc_map.find(id);

    if (search != id_rmc_map.end()) {
        reply.status = workers[0]->execute(id, reply, arg);
    } else {
        die("didn't find RMC");
    }

    return 0;
}

class NICServer {
    RDMAServer rserver;
    RMCScheduler &sched;

    bool nsready;
    size_t bsize;

    /* communication with client */
    std::vector<CmdRequest> req_buf;
    std::vector<CmdReply> reply_buf;
    ibv_mr *req_buf_mr;
    ibv_mr *reply_buf_mr;

    /* post an ibv recv for an incoming CmdRequest */
    void post_recv_req(CmdRequest *req);
    void post_send_reply(CmdReply *reply);
    /* send an unsignaled reply back to client */
    void post_send_uns_reply(CmdReply *reply);
    void dispatch(CmdRequest *req, CmdReply *reply);
    CmdRequest *get_req(size_t req_idx);
    CmdReply *get_reply(size_t req_idx);

    /* RMC entry points */
    void req_get_rmc_id(CmdRequest *req, CmdReply *reply);
    void req_call_rmc(CmdRequest *req, CmdReply *reply);

public:
    NICServer(RMCScheduler &s, size_t b) : sched(s), nsready(false), bsize(b) {
        assert(bsize > 0);
        req_buf.reserve(bsize);
        reply_buf.reserve(bsize);

        for (size_t i = 0; i < bsize; ++i) {
            req_buf.push_back(CmdRequest());
            reply_buf.push_back(CmdReply());
        }
    }

    void connect(int port);
    void handle_requests();
    void disconnect();
};

inline void NICServer::post_recv_req(CmdRequest *req)
{
    assert(nsready);
    rserver.post_recv(req, sizeof(CmdRequest), req_buf_mr->lkey);
}

inline void NICServer::post_send_reply(CmdReply *reply)
{
    assert(nsready);
    rserver.post_send(reply, sizeof(CmdReply), reply_buf_mr->lkey);
}

inline void NICServer::post_send_uns_reply(CmdReply *reply)
{
    assert(nsready);
    bool poll = rserver.post_send_unsignaled(reply, sizeof(CmdReply), reply_buf_mr->lkey);
    if (poll)
        rserver.poll_atleast(1, rserver.get_send_cq());
}

/* Compute the id for this rmc, if it doesn't exist, register it in map.
   Return the id */
inline void NICServer::req_get_rmc_id(CmdRequest *req, CmdReply *reply)
{
    assert(nsready);
    assert(req->type == CmdType::GET_RMCID);

    RMC rmc(req->request.getid.rmc);
    RMCId id = sched.get_rmc_id(rmc);

    /* get_id reply */
    reply->type = CmdType::GET_RMCID;
    reply->reply.getid.id = id;
    post_send_reply(reply);
    rserver.poll_exactly(1, rserver.get_send_cq());
}

inline void NICServer::req_call_rmc(CmdRequest *req, CmdReply *reply)
{
    assert(req->type == CmdType::CALL_RMC);
    CallReply &callreply = reply->reply.call;
    CallReq &callreq = req->request.call;

    size_t arg = std::stoull(callreq.data);
    sched.call_rmc(callreq.id, callreply, arg);

    reply->type = CmdType::CALL_RMC;
    post_send_uns_reply(reply);
}

inline CmdRequest *NICServer::get_req(size_t req_idx)
{
    return &req_buf[req_idx];
}

inline CmdReply *NICServer::get_reply(size_t rep_idx)
{
    return &reply_buf[rep_idx];
}

#endif
