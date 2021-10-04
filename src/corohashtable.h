#pragma once

#include "corormc.h"
#include "lib/rte_hash.h"

template <class T> inline CoroRMC hash_query(Backend<T> &b) {
  //thread_local uint32_t num_execs = 0;
  //int num_nodes = co_await b.get_param();
  //uintptr_t addr = b.get_baseaddr(num_nodes);
  //LLNode *node = nullptr;
  //bool lockreads = true;
  struct rte_hash *handle;
  //int pos0;
  const char *key = "hi";

  rte_hash_lookup(handle, key);

  //if (++num_execs >= 10) {
  //  lockreads = false;
  //  num_execs = 0;
  //}

  //for (int i = 0; i < num_nodes; ++i) {
  //  if (lockreads)
  //    read_lock();
  //  else
  //    write_lock();

  //  node = static_cast<LLNode *>(co_await b.read(addr, sizeof(LLNode)));
  //  addr = reinterpret_cast<uintptr_t>(node->next);

  //  if (lockreads)
  //    read_unlock();
  //  else
  //    write_unlock();
  //}

  co_yield 1;
}

