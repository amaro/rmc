#pragma once

#include <cstdlib>
#include <cstring>
#include <memory>

#include "allocator.h"
#include "rdma/rdmaserver.h"
#include "rmcs.h"

class HostServer {
  RDMAServer rserver;
  ServerAllocator SA;
  std::vector<ServerAlloc> allocs;
  bool hsready;
  std::unique_ptr<CtrlReq>
      ctrlreq;  // no CtrlReply since we don't need one right now.
  ibv_mr *ctrlreq_mr;

  void send_rdma_mr();

 public:
  HostServer(uint16_t num_qps)
      : rserver(num_qps, true), SA(rserver), hsready(false) {
    ctrlreq = std::make_unique<CtrlReq>();
  }

  ~HostServer() {}

  void connect_and_block(int port);
  void disconnect();
};
