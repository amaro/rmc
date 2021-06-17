#ifndef ONE_SIDED_CLIENT_H
#define ONE_SIDED_CLIENT_H

#include <coroutine>
#include "rdma/rdmaclient.h"
#include "hostserver.h"
#include "rmc.h"

class HostMemoryAsyncRead;

class OneSidedClient {
    RDMAClient rclient;

    bool onesready;
    ibv_mr host_mr;     // host's memory region; remote addr and rkey
    ibv_mr *req_buf_mr; // to send Cmd requests
    ibv_mr *rdma_mr;    // for 1:1 mapping of host's rdma buffer
    std::unique_ptr<CmdRequest> req_buf;
    char *rdma_buffer;

    void disconnect(); //TODO: do we need this?
    void recv_rdma_mr();

public:
    OneSidedClient(unsigned int num_qps) :
            rclient(num_qps), onesready(false) {
        rdma_buffer = static_cast<char *>(aligned_alloc(HostServer::PAGE_SIZE,
                                    HostServer::RDMA_BUFF_SIZE));
        req_buf = std::make_unique<CmdRequest>();
    }

    ~OneSidedClient() {
        free(rdma_buffer);
    }

    void connect(const std::string &ip, const unsigned int &port);
    void read_async(uint32_t offset, uint32_t size);
    HostMemoryAsyncRead readfromcoro(uint32_t offset, uint32_t size) noexcept;
    void writehost(uint64_t raddr, uint32_t size, void *localbuff);
    template<typename T> int poll_reads_atmost(int max, T&& comp_func);
    char *get_rdma_buffer();
    void *get_remote_base_addr();
    void *get_local_base_addr();

    RDMAClient &get_rclient();
};

class HostMemoryAsyncRead {
    OneSidedClient &client;
    uint32_t offset;
    uint32_t size;

public:
    HostMemoryAsyncRead(OneSidedClient &c, uint32_t o, uint32_t s) :
            client(c), offset(o), size(s) { }

    /* always suspend when we co_await HostMemoryAsyncRead */
    bool await_ready() const noexcept { return false; }

    /* called when await_ready() return false, so always */
    auto await_suspend(std::coroutine_handle<> awaitingcoro) {
        client.read_async(offset, size);
        return true; // suspend the coroutine
    }

    /* this is what's actually returned in the co_await
       we assume poll has been called, so memory is ready to be read */
    void *await_resume() {
        char *rdma_buff = client.get_rdma_buffer();
        return rdma_buff + offset;
    }
};

inline void OneSidedClient::recv_rdma_mr()
{
    assert(onesready);

    rclient.post_recv(rclient.get_ctrl_ctx(), req_buf.get(),
                        sizeof(CmdRequest), req_buf_mr->lkey);
    rclient.poll_exactly(1, rclient.get_recv_cq());

    assert(req_buf->type == SET_RDMA_MR);
    memcpy(&host_mr, &req_buf->request.rdma_mr.mr, sizeof(ibv_mr));
    LOG("received SET_RDMA_MR; rkey=" << host_mr.rkey);
}

/* assumes the mapping from host memory to nic memory is 1:1; i.e.
   regions are the same size.
   so the offsets are taken the same way remotely and locally */
inline void OneSidedClient::read_async(uint32_t offset, uint32_t size)
{
    assert(onesready);
    assert(rclient.batch_ctx != nullptr);

    uintptr_t remote_addr = reinterpret_cast<uintptr_t>(get_remote_base_addr()) + offset;
    uintptr_t local_addr = reinterpret_cast<uintptr_t>(get_local_base_addr()) + offset;

    RDMAContext *ctx = rclient.batch_ctx;
    ctx->post_batched_read(remote_addr, local_addr, size, host_mr.rkey, rdma_mr->lkey);
}

inline HostMemoryAsyncRead OneSidedClient::readfromcoro(uint32_t offset, uint32_t size) noexcept
{
    assert(onesready);

    return HostMemoryAsyncRead{*this, offset, size};
}

template<typename T>
inline int OneSidedClient::poll_reads_atmost(int max, T&& comp_func)
{
    assert(onesready);

    return rclient.poll_batched_atmost(max, rclient.get_send_compqueue(), comp_func);
}

inline char *OneSidedClient::get_rdma_buffer()
{
    return rdma_buffer;
}

inline void *OneSidedClient::get_remote_base_addr()
{
    return host_mr.addr;
}

inline void *OneSidedClient::get_local_base_addr()
{
    return rdma_mr->addr;
}

inline RDMAClient &OneSidedClient::get_rclient()
{
    return rclient;
}

#endif
