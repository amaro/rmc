#ifndef NIC_SERVER_H
#define NIC_SERVER_H

#include <functional>
#include <unordered_map>
#include "rdmaserver.h"
#include "rdmaclient.h"
#include "rmc.h"

class NICServer {
    std::unordered_map<RMCId, RMC> id_rmc_map;

    RDMAServer rserver;
    /* rmc server ready */
    bool nsready;
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
    NICServer() : nsready(false) {
        req_buf = std::make_unique<CmdRequest>();
        reply_buf = std::make_unique<CmdReply>();
    }

    void connect(int port);
    void handle_requests();
    void disconnect();
};

class NICClient {
    RDMAClient rclient;

    bool ncready;
    ibv_mr host_mr;

    void disconnect();

public:
    NICClient() : ncready(false) { }

    void connect(const std::string &ip, const std::string &port);
};


#endif
