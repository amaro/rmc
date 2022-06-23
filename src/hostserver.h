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
  const RMCType workload;
  HugeAllocator huge;

  void send_rdma_mr();

 public:
  HostServer(uint16_t num_qps, RMCType work)
      : rserver(num_qps, true),
        SA(rserver, huge),
        hsready(false),
        workload(work) {
    ctrlreq = std::make_unique<CtrlReq>();
  }

  ~HostServer() {}

  void connect_and_block(int port);
  void disconnect();
};
