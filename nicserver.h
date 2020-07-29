#ifndef NIC_SERVER_H
#define NIC_SERVER_H

#include <functional>
#include <unordered_map>
#include <cstdint>
#include "rdmaserver.h"
#include "rdmaclient.h"
#include "hostserver.h"
#include "rmc.h"

class NICClient {
    RDMAClient rclient;

    bool ncready;
    ibv_mr host_mr;     // host's memory region; remote addr and rkey
    ibv_mr *req_buf_mr; // to send Cmd requests
    ibv_mr *rdma_mr;    // for 1:1 mapping of host's rdma buffer
    std::unique_ptr<CmdRequest> req_buf;
    void *rdma_buffer;

    void disconnect(); //TODO: how is disconnect triggered?
    void recv_rdma_mr();

public:
    NICClient() : ncready(false) {
        rdma_buffer = (char *) aligned_alloc(HostServer::PAGE_SIZE,
                                             HostServer::RDMA_BUFF_SIZE);
        req_buf = std::make_unique<CmdRequest>();
    }

    ~NICClient() {
        free(rdma_buffer);
    }

    void connect(const std::string &ip, const std::string &port);
    void readhost(uint32_t offset, uint32_t size);
    void writehost(uint64_t raddr, uint32_t size, void *localbuff);
};

inline void NICClient::recv_rdma_mr()
{
    assert(ncready);

    rclient.post_recv(req_buf.get(), sizeof(CmdRequest), req_buf_mr->lkey);
    rclient.blocking_poll_nofunc(1);

    assert(req_buf->type == SET_RDMA_MR);
    memcpy(&host_mr, &req_buf->request.rdma_mr.mr, sizeof(ibv_mr));
    std::cout << "NICClient: received SET_RDMA_MR; rkey=" << host_mr.rkey << "\n";
}

inline void NICClient::readhost(uint32_t offset, uint32_t size)
{
    assert(ncready);
    rclient.post_read(*rdma_mr, host_mr, offset, size);
    rclient.blocking_poll_nofunc(1);
    std::cout << "read from host " << size << " bytes\n";
}

class RMCWorker {
    NICClient &rclient;
    unsigned id;

public:
    RMCWorker(NICClient &c, unsigned id) : rclient(c), id(id) {}
    int execute(const RMCId &id);
};

class RMCScheduler {
    const unsigned NUM_WORKERS = 1;

    std::unordered_map<RMCId, RMC> id_rmc_map;
    std::vector<std::unique_ptr<RMCWorker>> workers;
    NICClient &client;

public:
    RMCScheduler(NICClient &c) : client(c) {
        for (unsigned i = 0; i < NUM_WORKERS; ++i)
            workers.push_back(std::make_unique<RMCWorker>(client, i));
    }

    /* RMC entry points */
    RMCId get_rmc_id(const RMC &rmc);
    int call_rmc(const RMCId &id);
};

inline RMCId RMCScheduler::get_rmc_id(const RMC &rmc)
{
    RMCId id = std::hash<RMC>{}(rmc);

    if (id_rmc_map.find(id) == id_rmc_map.end()) {
        id_rmc_map.insert({id, rmc});
        std::cout << "registered new id=" << id << "for rmc=" << rmc << "\n";
    }

    return id;
}

inline int RMCScheduler::call_rmc(const RMCId &id)
{
    int res = EINVAL;
    auto search = id_rmc_map.find(id);

    if (search != id_rmc_map.end()) {
        std::cout << "Called RMC: " << search->second << "\n";
        return workers[0]->execute(id);
    }

    return res;
}

class NICServer {
    RDMAServer rserver;
    RMCScheduler &sched;

    /* rmc server ready */
    bool nsready;
    std::unique_ptr<CmdRequest> req_buf;
    std::unique_ptr<CmdReply> reply_buf;
    ibv_mr *req_buf_mr;
    ibv_mr *reply_buf_mr;

    /* post an ibv recv for an incoming CmdRequest */
    void post_recv_req();
    /* send a reply back to client */
    void post_send_reply();

    /* RMC entry points */
    void req_get_rmc_id();
    void req_call_rmc();

public:
    NICServer(RMCScheduler &s) : sched(s), nsready(false) {
        req_buf = std::make_unique<CmdRequest>();
        reply_buf = std::make_unique<CmdReply>();
    }

    void connect(int port);
    void handle_requests();
    void disconnect();
};

inline void NICServer::post_recv_req()
{
    assert(nsready);
    rserver.post_recv(req_buf.get(), sizeof(CmdRequest), req_buf_mr->lkey);
}

inline void NICServer::post_send_reply()
{
    assert(nsready);
    rserver.post_send(reply_buf.get(), sizeof(CmdReply), reply_buf_mr->lkey);
}

#endif
