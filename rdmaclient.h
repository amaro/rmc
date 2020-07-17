#ifndef RDMA_CLIENT_H
#define RDMA_CLIENT_H

#include "rdmapeer.h"

class RDMAClient: public RDMAPeer {

protected:
    // for client to receive info about rdma buffer
    std::unique_ptr<RDMAMessage> recv_msg;
    ibv_mr *recv_mr;

    void recv_buff_info();
    virtual ~RDMAClient() { }
    void handle_addr_resolved(rdma_cm_id *cm_id);
    void register_client_buffers();

public:
    RDMAClient() : RDMAPeer() {}

    /* client multi step connection establishment,
       assumes caller is client. Blocks until connection established */
    void connect_to_server(const std::string &ip, const std::string &port);

    void disconnect()
    {
        ibv_dereg_mr(recv_mr);
        ibv_dereg_mr(rdma_buffer_mr);
        RDMAPeer::disconnect();
    }
};

#endif
