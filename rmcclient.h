#ifndef RMC_CLIENT_H
#define RMC_CLIENT_H

#include "rdmaclient.h"
#include "rmc.h"

class RMCClient {

    RDMAClient rclient;
    /* rmc client ready */
    bool rmccready;
    std::unique_ptr<RMCRequest> req_buf;
    std::unique_ptr<RMCReply> reply_buf;
    ibv_mr *req_buf_mr;
    ibv_mr *reply_buf_mr;

public:
    RMCClient() : rmccready(false) {
        req_buf = std::make_unique<RMCRequest>();
        reply_buf = std::make_unique<RMCReply>();
    }

    void connect(const std::string &ip, const std::string &port);

    /* gets the id of an RMC.
       if it exists, simply returns the id;
       if it doesn't exist, creates it and returns the id */
    RMCId get_id(const RMC &rmc);

    /* calls an RMC by its id.
       TODO: figure out params, returns, etc. */
    int call(const RMCId &id);

    void disconnect();
};

inline void RMCClient::disconnect()
{
    assert(rmccready);
    rclient.dereg_mr(reply_buf_mr);
    rclient.dereg_mr(req_buf_mr);
    rclient.disconnect();
    rmccready = false;
}

#endif
