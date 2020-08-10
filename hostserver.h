#ifndef HOST_SERVER_H
#define HOST_SERVER_H

#include <cstdlib>
#include <cstring>
#include "rdmaserver.h"
#include "rmc.h"

class HostServer {
    RDMAServer rserver;
    bool hsready;
    char *rdma_buffer;
    // hostserver sends requests to nicserver
    std::unique_ptr<CmdRequest> req_buf;
    // no reply_buf since we don't need one right now.
    ibv_mr *rdma_mr;
    ibv_mr *req_buf_mr;

    void send_rdma_mr();

public:
    /* TODO: move these to a config.h or something */
    const static long RDMA_BUFF_SIZE = 1 << 20;
    const static int PAGE_SIZE = 4096;

    HostServer() : hsready(false) {
        rdma_buffer = static_cast<char *>(aligned_alloc(PAGE_SIZE, RDMA_BUFF_SIZE));
        for (size_t i = 0; i < RDMA_BUFF_SIZE; ++i)
            rdma_buffer[i] = (i + 1) % 255; // no 0 as it can be interpreted as escape char
        req_buf = std::make_unique<CmdRequest>();
    }

    ~HostServer() {
        free(rdma_buffer);
    }

    void connect_and_block(int port);

    void disconnect();
};

inline void HostServer::send_rdma_mr()
{
    assert(hsready);

    req_buf->type = CmdType::SET_RDMA_MR;
    memcpy(&req_buf->request.rdma_mr.mr, rdma_mr, sizeof(ibv_mr));
    rserver.post_send(req_buf.get(), sizeof(CmdRequest), req_buf_mr->lkey);
    rserver.blocking_poll_nofunc(1, rserver.get_send_cq());
    LOG("sent SET_RDMA_MR");
}

#endif
