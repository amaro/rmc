#ifndef RDMA_CLIENT_H
#define RDMA_CLIENT_H

#include "rdmapeer.h"

class RDMAClient: public RDMAPeer {
    void handle_addr_resolved(RDMAContext &ctx, rdma_cm_id *cm_id);

public:
    RDMAClient() : RDMAPeer() { }

    /* client multi step connection establishment,
       assumes caller is client. Blocks until connection established */
    void connect_to_server(const std::string &ip, const std::string &port);
    void disconnect_all();

};

#endif
