#include "hostserver.h"
#include "utils/utils.h"

void HostServer::connect_and_block(int port)
{
    assert(!hsready);

    /* accept connection */
    rserver.connect_events(port);

    /* register mrs */
    rdma_mr = rserver.register_mr(rdma_buffer, RDMA_BUFF_SIZE,
            IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_LOCAL_WRITE);

    LOG("rdma_mr rkey=" << rdma_mr->rkey);
    /* req_buf holds outgoing requests to nicserver */
    req_buf_mr = rserver.register_mr(req_buf.get(), sizeof(CmdRequest), 0);

    hsready = true;
    send_rdma_mr();

    rserver.disconnect_events();
}

void HostServer::disconnect()
{
    assert(hsready);

    LOG("received disconnect req");
    rserver.disconnect_events();
    hsready = false;
}

int main(int argc, char* argv[])
{
    if (argc != 2)
        die("usage: server <port>");

    HostServer server;
    server.connect_and_block(atoi(argv[1]));
}
