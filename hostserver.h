#ifndef HOST_SERVER_H
#define HOST_SERVER_H

#include <functional>
#include <unordered_map>
#include "rdmaserver.h"
#include "rmc.h"

class HostServer {

    std::unordered_map<RMCId, RMC> id_rmc_map;

    RDMAServer rserver;
    /* rmc server ready */
    bool rmcsready;
    std::unique_ptr<CmdRequest> req_buf;
    std::unique_ptr<CmdReply> reply_buf;
    ibv_mr *req_buf_mr;
    ibv_mr *reply_buf_mr;

    /* post an ibv recv for an incoming CmdRequest */
    void post_recv_req();
    /* send a reply back to client */
    void post_send_reply();

    /* RMC entry points */
    void get_rmc_id();
    void call_rmc();

public:
    HostServer() : rmcsready(false) {
        req_buf = std::make_unique<CmdRequest>();
        reply_buf = std::make_unique<CmdReply>();
    }

    void connect(int port);
    void handle_requests();
    void disconnect();
};

#endif
