#include "rdmapeer.h"

class RDMAServer: public RDMAPeer {

    // for server to send info about rdma buffer
    std::unique_ptr<Message> send_msg;
    ibv_mr *send_mr;

    void send_buff_info();

public:
    RDMAServer() : RDMAPeer() {}

    /* server multi step connection establishment
       assumes caller is server. Blocks until disconnect */
    void connect_from_client(int port);
    void handle_conn_request(rdma_cm_id *cm_id);
    void register_server_buffers();

    void disconnect()
    {
        ibv_dereg_mr(send_mr);
        ibv_dereg_mr(rdma_buffer_mr);
        RDMAPeer::disconnect();
    }
};
