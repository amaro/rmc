#include "hostserver.h"
#include "utils/utils.h"

void HostServer::connect_and_block(int port)
{
    assert(!hsready);

    /* accept connection */
    rserver.connect_from_client(port);

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

void HostServer::init_rdma_buffer()
{
    rdma_buffer = static_cast<char *>(aligned_alloc(PAGE_SIZE, RDMA_BUFF_SIZE));

    /* create linked list */
    size_t num_nodes = RDMA_BUFF_SIZE / sizeof(LLNode);
    linkedlist = new(rdma_buffer) LLNode[num_nodes];
    for (size_t n = 0; n < num_nodes - 1; ++n)
        linkedlist[n].next = &linkedlist[n + 1];
    linkedlist[num_nodes - 1].next = nullptr;
    LOG("linkedlist[0].next=" << linkedlist[0].next);
}

int main(int argc, char* argv[])
{
    if (argc != 3)
        die("usage: server <port> <numqps>");

    HostServer server(atoi(argv[2]));
    server.connect_and_block(atoi(argv[1]));
}
