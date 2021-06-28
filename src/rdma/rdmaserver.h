#ifndef RDMA_SERVER_H
#define RDMA_SERVER_H

#include <arpa/inet.h>
#include "rdmapeer.h"

class RDMAServer: public RDMAPeer {
    bool onesided;
public:
    RDMAServer(unsigned int num_qps, bool onesided) :
        RDMAPeer(num_qps), onesided(onesided) { }

    /* server multi step connection establishment
       assumes caller is server. Blocks until connection
       established */
    void connect_from_client(int port);
    void disconnect_events();
    void handle_conn_request(RDMAContext &ctx, rdma_cm_id *cm_id);
};



#endif
