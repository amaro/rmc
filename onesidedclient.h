#ifndef ONE_SIDED_CLIENT_H
#define ONE_SIDED_CLIENT_H

#include "rdmaclient.h"
#include "hostserver.h"
#include "rmc.h"

class OneSidedClient {
    RDMAClient rclient;

    bool onesready;
    ibv_mr host_mr;     // host's memory region; remote addr and rkey
    ibv_mr *req_buf_mr; // to send Cmd requests
    ibv_mr *rdma_mr;    // for 1:1 mapping of host's rdma buffer
    std::unique_ptr<CmdRequest> req_buf;
    char *rdma_buffer;

    void disconnect(); //TODO: how is disconnect triggered?
    void recv_rdma_mr();

public:
    OneSidedClient() : onesready(false) {
        rdma_buffer = static_cast<char *>(aligned_alloc(HostServer::PAGE_SIZE,
                                    HostServer::RDMA_BUFF_SIZE));
        req_buf = std::make_unique<CmdRequest>();
    }

    ~OneSidedClient() {
        free(rdma_buffer);
    }

    void connect(const std::string &ip, const std::string &port);
    void readhost(uint32_t offset, uint32_t size);
    void writehost(uint64_t raddr, uint32_t size, void *localbuff);
    char *get_rdma_buffer();
};

inline void OneSidedClient::recv_rdma_mr()
{
    assert(onesready);

    rclient.post_recv(req_buf.get(), sizeof(CmdRequest), req_buf_mr->lkey);
    rclient.blocking_poll_nofunc(1);

    assert(req_buf->type == SET_RDMA_MR);
    memcpy(&host_mr, &req_buf->request.rdma_mr.mr, sizeof(ibv_mr));
    LOG("received SET_RDMA_MR; rkey=" << host_mr.rkey);
}

inline void OneSidedClient::readhost(uint32_t offset, uint32_t size)
{
    assert(onesready);

    rclient.post_read(*rdma_mr, host_mr, offset, size);
    rclient.blocking_poll_nofunc(1);
    LOG("read from host " << size << " bytes");
}

inline char *OneSidedClient::get_rdma_buffer()
{
    return rdma_buffer;
}

#endif
