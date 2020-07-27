#ifndef NIC_SERVER_H
#define NIC_SERVER_H

#include <functional>
#include <unordered_map>
#include "rdmaserver.h"
#include "rdmaclient.h"
#include "rmc.h"

class NICClient {
    RDMAClient rclient;

    bool ncready;
    ibv_mr host_mr;

    void disconnect();

public:
    NICClient() : ncready(false) { }

    void connect(const std::string &ip, const std::string &port);
};

class RMCWorker {
    NICClient &client;

public:
    RMCWorker(NICClient &c) : client(c) {}
};

class RMCScheduler {
    std::unordered_map<RMCId, RMC> id_rmc_map;
    NICClient &client;

public:
    RMCScheduler(NICClient &c) : client(c) { }

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
        res = 0;
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




#endif
