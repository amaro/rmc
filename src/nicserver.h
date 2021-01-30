#ifndef NIC_SERVER_H
#define NIC_SERVER_H

#include <functional>
#include <unordered_map>
#include <cstdint>
#include "rdma/rdmaserver.h"
#include "onesidedclient.h"
#include "hostserver.h"
#include "scheduler.h"
#include "rmc.h"

class NICServer {
    RDMAServer rserver;
    RMCScheduler &sched;

    bool nsready;
    /* true if we received a disconnect req, so we are waiting for rmcs to
       finish executing before disconnecting */
    bool recvd_disconnect;
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
    void dispatch_new_req(CmdRequest *req);
    CmdRequest *get_req(size_t req_idx);
    CmdReply *get_reply(size_t req_idx);

    /* RMC entry points */
    void req_get_rmc_id(CmdRequest *req);
    void req_new_rmc(CmdRequest *req);

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
inline void NICServer::req_get_rmc_id(CmdRequest *req)
{
    assert(nsready);
    assert(req->type == CmdType::GET_RMCID);

    RMC rmc(req->request.getid.rmc);
    RMCId id = sched.get_rmc_id(rmc);

    /* for now, just use buffer 0 for get rmc id reply */
    CmdReply *reply = get_reply(0);
    reply->type = CmdType::GET_RMCID;
    reply->reply.getid.id = id;
    post_send_reply(reply);
    rserver.poll_exactly(1, rserver.get_send_cq());
}

inline void NICServer::req_new_rmc(CmdRequest *req)
{
    assert(req->type == CmdType::CALL_RMC);
    //CallReply &callreply = reply->reply.call;
    //CallReq &callreq = req->request.call;

    //size_t arg = std::stoull(callreq.data);
    sched.create_rmc();
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
