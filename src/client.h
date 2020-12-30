#ifndef RMC_CLIENT_H
#define RMC_CLIENT_H

#include <algorithm>
#include "rdma/rdmaclient.h"
#include "rmc.h"

class HostClient {
    bool rmccready;
    size_t bsize;

    RDMAClient rclient;

    /* communication with nicserver */
    std::vector<CmdRequest> req_buf;
    std::vector<CmdReply> reply_buf;
    ibv_mr *req_buf_mr;
    ibv_mr *reply_buf_mr;

    void post_recv_reply(CmdReply *reply);
    void post_send_req_unsig(CmdRequest *req);
    void post_send_req(CmdRequest *req);
    CmdRequest *get_req(size_t req_idx);
    CmdReply *get_reply(size_t rep_idx);
    void disconnect();

public:
    HostClient(size_t b) : rmccready(false), bsize(b) {
        assert(bsize > 0);
        req_buf.reserve(bsize);
        reply_buf.reserve(bsize);

        for (size_t i = 0; i < bsize; ++i) {
            req_buf.push_back(CmdRequest());
            reply_buf.push_back(CmdReply());
        }
    }

    void connect(const std::string &ip, const std::string &port);

    /* 1. post recv for id
       2. send rmc to server; wait for 1.
       3. return id */
    RMCId get_rmc_id(const RMC &rmc);

    /* calls an RMC by its id.
       TODO: figure out params, returns, etc. */
    int call_rmc(const RMCId &id, const size_t arg, long long &duration);
    int call_one_rmc(const RMCId &id, const size_t arg, long long &duration);

    /* cmd to initiate disconnect */
    void last_cmd();

    void parse_rmc_reply(CmdReply *reply) const;
    void arm_call_req(CmdRequest *req, const RMCId &id, const size_t arg);
};

/* post a recv for CmdReply */
inline void HostClient::post_recv_reply(CmdReply *reply)
{
    assert(rmccready);

    rclient.post_recv(reply, sizeof(CmdReply), reply_buf_mr->lkey);
}

inline void HostClient::post_send_req(CmdRequest *req)
{
    assert(rmccready);

    rclient.post_send(req, sizeof(CmdRequest), req_buf_mr->lkey);
}

inline void HostClient::post_send_req_unsig(CmdRequest *req)
{
    assert(rmccready);

    bool poll = rclient.post_send_unsignaled(req, sizeof(CmdRequest), req_buf_mr->lkey);
    if (poll)
        rclient.poll_atleast(1, rclient.get_send_cq());
}

inline CmdRequest *HostClient::get_req(size_t req_idx)
{
    return &req_buf[req_idx];
}

inline CmdReply *HostClient::get_reply(size_t rep_idx)
{
    return &reply_buf[rep_idx];
}

inline void HostClient::parse_rmc_reply(CmdReply *reply) const
{
    CallReply *callreply = &reply->reply.call;
    size_t hash = std::stoull(callreply->data);
    LOG("hash at client=" << hash);
}

inline void HostClient::arm_call_req(CmdRequest *req, const RMCId &id, const size_t arg)
{
    req->type = CmdType::CALL_RMC;
    CallReq *callreq = &req->request.call;
    callreq->id = id;
    num_to_str<size_t>(arg, callreq->data, MAX_RMC_ARG_LEN);
}

#endif
