#ifndef RMC_SERVER_H
#define RMC_SERVER_H

#include <functional>
#include <unordered_map>
#include "rdmaserver.h"
#include "rmc.h"

class RMCServer {

    std::unordered_map<RMCId, RMC> id_rmc_map;

    RDMAServer rserver;
    /* rmc server ready */
    bool rmcsready;
    std::unique_ptr<RMCRequest> req_buf;
    std::unique_ptr<RMCReply> reply_buf;
    ibv_mr *req_buf_mr;
    ibv_mr *reply_buf_mr;

    /* post an ibv recv for an incoming RMCRequest */
    void post_recv_req();
    /* send a reply back to client */
    void post_send_reply();

    /* RMC entry points */
    void get_rmc_id();
    void call_rmc();

public:
    RMCServer() : rmcsready(false) {
        req_buf = std::make_unique<RMCRequest>();
        reply_buf = std::make_unique<RMCReply>();
    }

    void connect(int port);
    void handle_requests();
    void disconnect();
};

#endif
