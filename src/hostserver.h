#pragma once

#include <cstdlib>
#include <cstring>
#include <memory>

#include "allocator.h"
#include "rdma/rdmaserver.h"
#include "rmcs.h"

class HostServer {
  RDMAServer rserver;
  RdmaMrAllocator SA;
  std::vector<MemoryRegion> allocs;
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

  ~HostServer() = default;
  HostServer(const HostServer &) = delete;
  HostServer(HostServer &&) = delete;
  HostServer &operator=(const HostServer &) = delete;
  HostServer &operator=(HostServer &&) = delete;

  void connect_and_block(int port);
  void disconnect();
};
