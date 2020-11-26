#include "rdmaclient.h"

void RDMAClient::connect_to_server(const std::string &ip, const std::string &port)
{
    addrinfo *addr = nullptr;
    rdma_cm_event *event = nullptr;
    event_channel = nullptr;

    TEST_NZ(getaddrinfo(ip.c_str(), port.c_str(), nullptr, &addr));
    TEST_Z(event_channel = rdma_create_event_channel());
    TEST_NZ(rdma_create_id(event_channel, &this->id, nullptr, RDMA_PS_TCP));
    TEST_NZ(rdma_resolve_addr(this->id, nullptr, addr->ai_addr, TIMEOUT_MS));

    freeaddrinfo(addr);

    while (rdma_get_cm_event(event_channel, &event) == 0) {
        rdma_cm_event event_copy;

        memcpy(&event_copy, event, sizeof(*event));
        rdma_ack_cm_event(event);
        event = &event_copy;

        if (event->event == RDMA_CM_EVENT_ADDR_RESOLVED) {
            handle_addr_resolved(event->id);
        } else if (event->event == RDMA_CM_EVENT_ROUTE_RESOLVED) {
            LOG("route resolved");
            connect_or_accept(true); // connect
        } else if (event->event == RDMA_CM_EVENT_ESTABLISHED) {
            handle_conn_established(event->id);
            return;
        } else {
            die("unknown or unexpected event.");
        }
    }
}

void RDMAClient::handle_addr_resolved(rdma_cm_id *cm_id)
{
    assert(!connected);
    LOG("address resolved");

    create_context(id->verbs);
    this->id = cm_id;
    create_qps();

    TEST_NZ(rdma_resolve_route(cm_id, TIMEOUT_MS));
}
