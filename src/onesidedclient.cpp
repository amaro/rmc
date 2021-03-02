#include "onesidedclient.h"

void OneSidedClient::connect(const std::string &ip, const std::string &port)
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

/* called when await_ready() return false, so always */
bool HostMemoryAsyncRead::await_suspend(std::coroutine_handle<> awaitingcoro) {
    request_handler->post_read(offset, size);
    return true; // suspend the coroutine
}

