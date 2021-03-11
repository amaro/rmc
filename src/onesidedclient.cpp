#include "onesidedclient.h"

void OneSidedClient::connect(const std::string &ip, const unsigned int &port)
{
    assert(!onesready);
    rclient.connect_to_server(ip, port);

    req_buf_mr = rclient.register_mr(req_buf.get(), sizeof(CmdRequest),
                                    IBV_ACCESS_LOCAL_WRITE);
    rdma_mr = rclient.register_mr(rdma_buffer, HostServer::RDMA_BUFF_SIZE,
                                    IBV_ACCESS_LOCAL_WRITE);

    onesready = true;
    recv_rdma_mr();
}
