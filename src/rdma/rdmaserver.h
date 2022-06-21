#ifndef RDMA_SERVER_H
#define RDMA_SERVER_H

#include "rdmapeer.h"

class RDMAServer : public RDMAPeer {
  bool onesided;

 public:
  // num_cqs always 1 here
  RDMAServer(uint16_t num_qps, bool onesided)
      : RDMAPeer(num_qps, 1), onesided(onesided) {}

  ~RDMAServer() {}
  /* server multi step connection establishment
     assumes caller is server. Blocks until connection
     established */
  void connect_from_client(int port);
  void disconnect_events();
  void handle_conn_request(RDMAContext &ctx, rdma_cm_id *cm_id);
};

#endif
