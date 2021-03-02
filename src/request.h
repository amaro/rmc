#ifndef REQUEST_H
#define REQUEST_H

#include <coroutine>
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <unordered_map>
#include <queue>
#include <rdma/rdma_cma.h>

#include "rmc.h"
#include "onesidedclient.h"

class OneSidedClient;
template <typename T> class CoroRMC;

class ReadRequest {
    uint32_t offset;
    uint32_t len;

public:
    ReadRequest(uint32_t offset, uint32_t len): offset(offset), len(len) {}
    void post(OneSidedClient &client);
};

class RMCRequestHandler {
public:
    void post(ReadRequest &req);
    void post_read(uint32_t offset, uint32_t len);

    inline bool is_mem_ready() {
        return mem_queue->size() < RDMAPeer::MAX_QP_INFLIGHT_READS;
    }

private:
    friend class RMCScheduler;

    bool batch_in_progress = false;

    OneSidedClient *client;
    
    /* Current RMC being processed */
    CoroRMC<int> *next_rmc = nullptr;


    std::queue<CoroRMC<int>*> *run_queue;

    /* RMCs waiting for host memory accesses */
    std::queue<CoroRMC<int>*> *mem_queue;

    /* RMC requests waiting to be sent */
    std::queue<std::pair<CoroRMC<int>*, ReadRequest>> *buffer_queue;

    void start_rmc_processing(CoroRMC<int> *rmc);
    void end_rmc_processing(bool is_rmc_done);

    void start_batched_ops();
    void end_batched_ops();
};

#endif
