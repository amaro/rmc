#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <coroutine>
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <unordered_map>
#include <rdma/rdma_cma.h>

#include "rmc.h"
#include "onesidedclient.h"

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

  using corotype = std::coroutine_handle<promise_type>;

  /* constructors */
  CoroRMC() noexcept : coroutine(nullptr) {}
  CoroRMC(corotype h) : coroutine(h) {}
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
    bool atfinalsuspend = coroutine.done();
    assert(not atfinalsuspend);
    coroutine.resume();
    return not atfinalsuspend;
  }

private:
  corotype coroutine;
};

class HostMemory;

/* trying to make this an awaitable */
class HostMemoryWriteOp {
public:
  HostMemoryWriteOp(const char *buf) {
    std::cout << "(not really) writing=" << buf << "\n";
  }
  /* always suspend when we co_await HostMemoryWriteOp */
  bool await_ready() const noexcept { return false; }
  /* suspend is called when await_ready() returns false.
   returning true here means suspend the coroutine */
  auto await_suspend(std::coroutine_handle<> awaitingcoro) {
    std::cout << "HostMemoryWriteOp::await_suspend\n";
    return true;
  }
  void await_resume() { std::cout << "HostMemoryWriteOp::await_resume\n"; }
};

class HostMemory {
public:
  HostMemory() noexcept {};

  HostMemoryWriteOp write(const char *buf) noexcept {
    return HostMemoryWriteOp{buf};
  }
};

/* one RMCScheduler per NIC core */
class RMCScheduler {
    std::unordered_map<RMCId, RMC> id_rmc_map;
    OneSidedClient &client;

public:
    RMCScheduler(OneSidedClient &c) : client(c) { }

    /* RMC entry points */
    RMCId get_rmc_id(const RMC &rmc);
    int call_rmc(const RMCId &id, CallReply &reply, size_t arg);
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

#endif
