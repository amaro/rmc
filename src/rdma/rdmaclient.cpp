#include "rdmaclient.h"

void RDMAClient::connect_to_server(const std::string &ip,
                                   const unsigned int &port) {
  addrinfo *addr = nullptr;
  rdma_cm_event *event = nullptr;
  char port_str[256] = "";

  for (auto qp = 0u; qp < this->num_qps; ++qp) {
    RDMAContext ctx(qp);
    snprintf(port_str, sizeof port_str, "%u", port + qp);

    TEST_NZ(getaddrinfo(ip.c_str(), port_str, nullptr, &addr));
    TEST_Z(ctx.event_channel = rdma_create_event_channel());
    TEST_NZ(
        rdma_create_id(ctx.event_channel, &ctx.cm_id, nullptr, RDMA_PS_TCP));
    TEST_NZ(rdma_resolve_addr(ctx.cm_id, nullptr, addr->ai_addr, TIMEOUT_MS));

    freeaddrinfo(addr);

    while (rdma_get_cm_event(ctx.event_channel, &event) == 0) {
      rdma_cm_event event_copy;

      memcpy(&event_copy, event, sizeof(*event));
      rdma_ack_cm_event(event);
      event = &event_copy;

      if (event->event == RDMA_CM_EVENT_ADDR_RESOLVED) {
        handle_addr_resolved(ctx, event->id);
      } else if (event->event == RDMA_CM_EVENT_ROUTE_RESOLVED) {
        connect_or_accept(ctx, true);  // connect
      } else if (event->event == RDMA_CM_EVENT_ESTABLISHED) {
        handle_conn_established(ctx);
        break;
      } else {
        die("unknown or unexpected event = %s\n", rdma_event_str(event->event));
      }
    }

    contexts.push_back(std::move(ctx));
  }
}

void RDMAClient::handle_addr_resolved(RDMAContext &ctx, rdma_cm_id *cm_id) {
  assert(!ctx.connected);

  if (!pds_cqs_created) create_pds_cqs(cm_id->verbs, onesided);

  ctx.cm_id = cm_id;
  create_qps(ctx, onesided);

  TEST_NZ(rdma_resolve_route(cm_id, TIMEOUT_MS));
}

void RDMAClient::disconnect_all() {
  dereg_mrs();

  for (auto &ctx : contexts) ctx.disconnect();

  destroy_pds_cqs();
}
