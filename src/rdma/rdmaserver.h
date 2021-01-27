#ifndef RDMA_SERVER_H
#define RDMA_SERVER_H

#include <arpa/inet.h>
#include "rdmapeer.h"

class RDMAServer: public RDMAPeer {

    rdma_cm_id *listen_id;

public:
    RDMAServer() : RDMAPeer() { }

    /* server multi step connection establishment
       assumes caller is server. Blocks until connection
       established */
    void connect_events(int port);
    void disconnect_events();
    void handle_conn_request(rdma_cm_id *cm_id);
    void disconnect();
};

inline void RDMAServer::disconnect()
{
    dereg_mrs();

    RDMAPeer::disconnect();

    rdma_destroy_id(listen_id);
    rdma_destroy_event_channel(event_channel);
}

#endif
