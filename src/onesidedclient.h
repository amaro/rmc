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
    void read_async(uint32_t offset, uint32_t size);
    HostMemoryAsyncRead readfromcoro(uint32_t offset, uint32_t size) noexcept;
    void writehost(uint64_t raddr, uint32_t size, void *localbuff);
    int poll_reads_atmost(int max);
    char *get_rdma_buffer();
    void *get_remote_base_addr();
    void post_read(const ibv_mr &local_mr, const ibv_mr &remote_mr,
                    uint32_t offset, uint32_t len);
    void start_batched_ops();
    void end_batched_ops();
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

    rclient.post_recv(req_buf.get(), sizeof(CmdRequest), req_buf_mr->lkey);
    rclient.poll_exactly(1, rclient.get_recv_cq());

    assert(req_buf->type == SET_RDMA_MR);
    memcpy(&host_mr, &req_buf->request.rdma_mr.mr, sizeof(ibv_mr));
    LOG("received SET_RDMA_MR; rkey=" << host_mr.rkey);
}

inline void OneSidedClient::readhost(uint32_t offset, uint32_t size)
{
    assert(onesready);

    start_batched_ops();
    post_read(*rdma_mr, host_mr, offset, size);
    end_batched_ops();
    rclient.poll_exactly(1, rclient.get_send_cq());
}

inline void OneSidedClient::read_async(uint32_t offset, uint32_t size)
{
    assert(onesready);

    post_read(*rdma_mr, host_mr, offset, size);
}

inline HostMemoryAsyncRead OneSidedClient::readfromcoro(uint32_t offset, uint32_t size) noexcept
{
    assert(onesready);

    return HostMemoryAsyncRead{*this, offset, size};
}

inline int OneSidedClient::poll_reads_atmost(int max)
{
    assert(onesready);

    return rclient.poll_atmost(max, rclient.get_send_cq());
}

inline char *OneSidedClient::get_rdma_buffer()
{
    return rdma_buffer;
}

inline void *OneSidedClient::get_remote_base_addr()
{
    return host_mr.addr;
}

/* for now assumes the mapping from host memory to nic memory is 1:1; i.e.
   regions are the same size.
   so the offsets are taken the same way remotely and locally */
inline void OneSidedClient::post_read(const ibv_mr &local_mr, const ibv_mr &remote_mr,
                                uint32_t offset, uint32_t len)
{
    uintptr_t raddr = reinterpret_cast<uintptr_t>(remote_mr.addr) + offset;
    uintptr_t laddr = reinterpret_cast<uintptr_t>(local_mr.addr) + offset;

    ibv_qp_ex *qpx = rclient.get_qpx();
    qpx->wr_flags = IBV_SEND_SIGNALED;
    ibv_wr_rdma_read(qpx, remote_mr.rkey, raddr);
    ibv_wr_set_sge(qpx, local_mr.lkey, laddr, len);
}

inline void OneSidedClient::start_batched_ops()
{
    ibv_qp_ex *qpx = rclient.get_qpx();
    ibv_wr_start(qpx);
}

inline void OneSidedClient::end_batched_ops()
{
    ibv_qp_ex *qpx = rclient.get_qpx();
    TEST_NZ(ibv_wr_complete(qpx));
}

#endif
