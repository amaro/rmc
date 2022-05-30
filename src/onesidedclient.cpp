#include "onesidedclient.h"

void OneSidedClient::connect(const std::string &ip, const unsigned int &port) {
  assert(!onesready);
  rclient.connect_to_server(ip, port);

  req_buf_mr =
      rclient.register_mr(req_buf.get(), sizeof(CmdRequest),
                          IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_RELAXED_ORDERING);
  rdma_mr =
      rclient.register_mr(rdma_buffer, RMCK_TOTAL_BUFF_SZ,
                          IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_RELAXED_ORDERING);

  onesready = true;
  recv_rdma_mr();

  puts("connected to hostserver.");
}
