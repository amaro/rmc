#include "onesidedclient.h"

void OneSidedClient::connect(const std::string &ip, const unsigned int &port) {
  assert(!onesready);
  rclient.connect_to_server(ip, port);

  ctrlreq_mr =
      rclient.register_mr(ctrlreq_buf.get(), sizeof(CtrlReq),
                          IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_RELAXED_ORDERING);
  onesready = true;
  recv_ctrl_reqs();

  puts("connected to hostserver.");
}
