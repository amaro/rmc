#ifndef RMC_SERVER_H
#define RMC_SERVER_H

#include "rdmaserver.h"
#include "rmc.h"

class RMCServer {

    RDMAServer rserver;
    /* rmc server ready */
    bool rmcsready;
    std::unique_ptr<RMCRequest> req_buf;
    std::unique_ptr<RMCReply> reply_buf;
    ibv_mr *req_buf_mr;
    ibv_mr *reply_buf_mr;

public:
    RMCServer() : rmcsready(false) {
        req_buf = std::make_unique<RMCRequest>();
        reply_buf = std::make_unique<RMCReply>();
    }

    void listen(int port);

    /* 1. post recv for id
       2. send rmc to server; wait for 1.
       3. return id */
    RMCId get_id(const RMC &rmc);
    int call(const RMCId &id);
    void disconnect();
};

inline void RMCServer::disconnect()
{
    assert(rmcsready);
    rserver.dereg_mr(req_buf_mr);
    rserver.dereg_mr(reply_buf_mr);
    rserver.disconnect();
    rmcsready = false;
}

#endif
