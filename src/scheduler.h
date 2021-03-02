#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <coroutine>
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <unordered_map>
#include <queue>
#include <rdma/rdma_cma.h>

#include "request.h"
#include "rmc.h"
#include "nicserver.h"

template <typename T = void> class CoroRMC {
/*
based on:
    https://www.modernescpp.com/index.php/c-20-an-infinite-data-stream-with-coroutines
    https://github.com/andreasbuhr/cppcoro/blob/master/include/cppcoro/task.hpp
*/
public:
  using value_type = T;

  /* must have this name */
  struct promise_type {
    promise_type() noexcept {};
    ~promise_type() = default;

    auto initial_suspend() { return std::suspend_always{}; }

    auto final_suspend() { return std::suspend_always{}; }

    auto get_return_object() noexcept {
      return CoroRMC{std::coroutine_handle<promise_type>::from_promise(*this)};
    }

    auto return_void() { return std::suspend_never{}; }

    auto yield_value(const value_type value) noexcept {
      current_value = value;
      return std::suspend_always{};
    }

    void unhandled_exception() noexcept { std::exit(1); }

    value_type current_value;
  };

  /* constructors */
  CoroRMC() noexcept : coroutine(nullptr) {}
  CoroRMC(std::coroutine_handle<promise_type> h) : coroutine(h) {}
  CoroRMC(CoroRMC &&oth) noexcept : coroutine(oth.coroutine) {
    oth.coroutine = nullptr;
  }
  CoroRMC &operator=(CoroRMC &&oth) noexcept {
    coroutine = oth.coroutine;
    oth.coroutine = nullptr;
    return *this;
  }
  CoroRMC(const CoroRMC &) = delete;
  CoroRMC &operator=(const CoroRMC &) = delete;

  ~CoroRMC() {
    if (coroutine)
      coroutine.destroy();
  }

  /* returns true if coroutine is done; false otherwise */
  bool resume() {
    /* coroutine.done() returns true if the coroutine is suspended at its final
    suspend point, or false if the coroutine is suspended at other suspend
    points. The behavior is undefined if it does not refer to a suspended
    coroutine. */
    assert(not coroutine.done());
    coroutine.resume();
    return coroutine.done();
  }

  int id;

private:
  std::coroutine_handle<promise_type> coroutine;
};

class ReadRequest;
class RMCRequestHandler;
class NICServer;

/* one RMCScheduler per NIC core */
class RMCScheduler {
    NICServer &ns;

    std::unordered_map<RMCId, RMC> id_rmc_map;

    /* RMCs ready to be run */
    std::queue<CoroRMC<int>*> run_queue;

    /* RMCs waiting for host memory accesses */
    std::queue<CoroRMC<int>*> mem_queue;

    /* RMC requests waiting to be sent to the host */
    std::queue<std::pair<CoroRMC<int>*, ReadRequest>> buffer_queue;
    
    /* Handles new RDMA requests for RMCs */
    RMCRequestHandler request_handler;

    size_t num_llnodes;
    /* true if we received a disconnect req, so we are waiting for rmcs to
       finish executing before disconnecting */
    bool recvd_disconnect = false;

public:
    RMCScheduler(NICServer &nicserver);

    /* RMC entry points */
    void run();
    void schedule();
    void set_num_llnodes(size_t num_nodes);
    bool executing();
    void dispatch_new_req(CmdRequest *req);

    /* RMC entry points */
    RMCId get_rmc_id(const RMC &rmc);
    void req_get_rmc_id(CmdRequest *req);
    void req_new_rmc(CmdRequest *req);
};

inline RMCId RMCScheduler::get_rmc_id(const RMC &rmc)
{
    RMCId id = std::hash<RMC>{}(rmc);

    if (id_rmc_map.find(id) == id_rmc_map.end()) {
        id_rmc_map.insert({id, rmc});
        LOG("registered new id=" << id << "for rmc=" << rmc);
    }

    return id;
}

inline void RMCScheduler::set_num_llnodes(size_t num_nodes)
{
    LOG("num nodes in each linked list=" << num_nodes);
    num_llnodes = num_nodes;
}

inline bool RMCScheduler::executing()
{
    return !run_queue.empty() || !mem_queue.empty();
}

inline void RMCScheduler::dispatch_new_req(CmdRequest *req)
{
    switch (req->type) {
    case CmdType::GET_RMCID:
        return req_get_rmc_id(req);
    case CmdType::CALL_RMC:
        return req_new_rmc(req);
    case CmdType::LAST_CMD:
        this->recvd_disconnect = true;
        return;
    default:
        DIE("unrecognized CmdRequest type");
    }
}

#endif
