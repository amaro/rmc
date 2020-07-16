#include "rdmapeer.h"

class RDMAClient: public RDMAPeer {

    // for client to receive info about rdma buffer
    std::unique_ptr<Message> recv_msg;
    ibv_mr *recv_mr;

    void recv_buff_info();

public:
    RDMAClient() : RDMAPeer() {}

    /* client multi step connection establishment,
       assumes caller is client. Blocks until connection established */
    void connect_to_server(const std::string &ip, const std::string &port);
    void register_client_buffers();
    void handle_addr_resolved(rdma_cm_id *cm_id);

    void disconnect()
    {
        ibv_dereg_mr(recv_mr);
        ibv_dereg_mr(rdma_buffer_mr);
        RDMAPeer::disconnect();
    }
};
