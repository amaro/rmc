#ifndef RDMA_CLIENT_H
#define RDMA_CLIENT_H

#include "rdmapeer.h"

class RDMAClient: public RDMAPeer {

protected:
    // for client to receive info about rdma buffer
    std::unique_ptr<RDMAMessage> recv_msg;
    ibv_mr *recv_mr;

    void handle_addr_resolved(rdma_cm_id *cm_id);

public:
    RDMAClient() : RDMAPeer() {
        recv_msg = std::make_unique<RDMAMessage>();
    }

    /* client multi step connection establishment,
       assumes caller is client. Blocks until connection established */
    void connect_to_server(const std::string &ip, const std::string &port);
    void disconnect();
};

inline void RDMAClient::disconnect()
{
    dereg_mrs();

    RDMAPeer::disconnect();
    rdma_destroy_event_channel(event_channel);
}

#endif
