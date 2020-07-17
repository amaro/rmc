#ifndef RDMA_SERVER_H
#define RDMA_SERVER_H

#include "rdmapeer.h"

class RDMAServer: public RDMAPeer {

    // for server to send info about rdma buffer
    std::unique_ptr<RDMAMessage> send_msg;
    ibv_mr *send_mr;

    void send_buff_info();

public:
    RDMAServer() : RDMAPeer() {}

    /* server multi step connection establishment
       assumes caller is server. Blocks until connection
       established */
    void connect_events(int port);
    void disconnect_events();
    void handle_conn_request(rdma_cm_id *cm_id);
    void register_server_buffers();

    void disconnect()
    {
        ibv_dereg_mr(send_mr);
        ibv_dereg_mr(rdma_buffer_mr);
        RDMAPeer::disconnect();
    }
};

#endif
