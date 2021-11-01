#pragma once

#ifdef WORKLOAD_SHAREDLOG

#include "corormc.h"
#include "log.h"

static uint64_t value = 0XDEADBEEF;

template <class T> inline CoroRMC log_append(Backend<T> &b) {
  { // Critical Section
    co_await write_lock();

    auto &log = b.log;
    if (log.get_capacity() - log.get_size() == 0) {
      log.trim(log.get_capacity());
    }

    co_await b.write_laddr(log.append(), static_cast<void*>(&value), sizeof(uint64_t));
    write_unlock();
  }

  co_yield 1;
}

template <class T> inline CoroRMC log_tail(Backend<T> &b) {
  { // Critical Section
    co_await read_lock();
    co_await b.read_laddr(b.log.tail(), sizeof(uint64_t));
    read_unlock();
  }

  co_yield 1;
}

template <class T> inline CoroRMC log_trim(Backend<T> &b) {
  { // Critical Section
    co_await write_lock();

    auto &log = b.log;
    log.trim(1);

    write_unlock();
  }
  co_yield 1;
}

#endif // WORKLOAD_SHAREDLOG
