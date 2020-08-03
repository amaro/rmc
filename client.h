#ifndef RMC_CLIENT_H
#define RMC_CLIENT_H

#include <algorithm>
#include "rdmaclient.h"
#include "rmc.h"

class HostClient {

    RDMAClient rclient;
    /* rmc client ready */
    bool rmccready;
    std::unique_ptr<CmdRequest> req_buf;
    std::unique_ptr<CmdReply> reply_buf;
    ibv_mr *req_buf_mr;
    ibv_mr *reply_buf_mr;

    void post_recv_reply();
    void post_send_req();

    void disconnect();

public:
    HostClient() : rmccready(false) {
        req_buf = std::make_unique<CmdRequest>();
        reply_buf = std::make_unique<CmdReply>();
    }

    void connect(const std::string &ip, const std::string &port);

    /* 1. post recv for id
       2. send rmc to server; wait for 1.
       3. return id */
    RMCId get_rmc_id(const RMC &rmc);

    /* calls an RMC by its id.
       TODO: figure out params, returns, etc. */
    int call_rmc(const RMCId &id);

    /* cmd to initiate disconnect */
    void last_cmd();

    void parse_rmc_reply() const;
};

/* post a recv for CmdReply */
inline void HostClient::post_recv_reply()
{
    assert(rmccready);

    rclient.post_recv(reply_buf.get(), sizeof(CmdReply), reply_buf_mr->lkey);
}

inline void HostClient::post_send_req()
{
    assert(rmccready);

    rclient.post_send(req_buf.get(), sizeof(CmdRequest), req_buf_mr->lkey);
}

inline void HostClient::parse_rmc_reply() const
{
    CallReply *reply = &reply_buf->reply.call;
    size_t hash = std::stoull(reply->data);
    LOG("hash at client=" << hash);
}

#endif
