#ifndef RDMA_CLIENT_H
#define RDMA_CLIENT_H

#include "rdmapeer.h"

class RDMAClient : public RDMAPeer {
  const bool onesided;
  void handle_addr_resolved(RDMAContext &ctx, rdma_cm_id *cm_id);

public:
  RDMAClient(unsigned int num_qps, bool onesided)
      : RDMAPeer(num_qps), onesided(onesided) {}

  /* client multi step connection establishment,
     assumes caller is client. Blocks until connection established */
  void connect_to_server(const std::string &ip, const unsigned int &port);
  void disconnect_all();
};

#endif
