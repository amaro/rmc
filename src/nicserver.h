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

class RMCScheduler;

class NICServer {
    friend class RMCScheduler;

    OneSidedClient &rclient;
    RDMAServer &rserver;

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

    CmdRequest *get_req(size_t req_idx);
    CmdReply *get_reply(size_t req_idx);

public:
    NICServer(OneSidedClient &client, RDMAServer &server, size_t b) : rclient(client),
            rserver(server), nsready(false), bsize(b) {
        assert(bsize > 0);
        req_buf.reserve(bsize);
        reply_buf.reserve(bsize);

        for (size_t i = 0; i < bsize; ++i) {
            req_buf.push_back(CmdRequest());
            reply_buf.push_back(CmdReply());
            /* assume replies are successful */
            reply_buf[i].type = CmdType::CALL_RMC;
            reply_buf[i].reply.call.status = 0;
        }
    }

    void connect(const unsigned int &port);
    void start(RMCScheduler &sched, const std::string &hostaddr,
                const unsigned int &hostport, const unsigned int &clientport);
    void init(RMCScheduler &sched);
    void disconnect();
};

inline void NICServer::post_recv_req(CmdRequest *req)
{
    assert(nsready);
    rserver.post_recv(rserver.get_context(0), req, sizeof(CmdRequest),
                        req_buf_mr->lkey);
}

inline void NICServer::post_send_reply(CmdReply *reply)
{
    assert(nsready);
    rserver.post_send(rserver.get_context(0), reply, sizeof(CmdReply),
                        reply_buf_mr->lkey);
}

inline void NICServer::post_send_uns_reply(CmdReply *reply)
{
    assert(nsready);
    bool poll = rserver.post_2s_send_unsig(rserver.get_context(0), reply,
                                            sizeof(CmdReply), reply_buf_mr->lkey);
    if (poll)
        rserver.poll_atleast(1, rserver.get_send_cq());
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
