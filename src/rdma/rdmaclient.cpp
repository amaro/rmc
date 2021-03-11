#include "rdmaclient.h"

void RDMAClient::connect_to_server(const std::string &ip, const std::string &port)
{
    addrinfo *addr = nullptr;
    rdma_cm_event *event = nullptr;

    TEST_NZ(getaddrinfo(ip.c_str(), port.c_str(), nullptr, &addr));

    for (unsigned int qp = 0; qp < this->num_qps; ++qp) {
        RDMAContext ctx;

        TEST_Z(ctx.event_channel = rdma_create_event_channel());
        TEST_NZ(rdma_create_id(ctx.event_channel, &ctx.id, nullptr, RDMA_PS_TCP));
        TEST_NZ(rdma_resolve_addr(ctx.id, nullptr, addr->ai_addr, TIMEOUT_MS));

        while (rdma_get_cm_event(ctx.event_channel, &event) == 0) {
            rdma_cm_event event_copy;

            memcpy(&event_copy, event, sizeof(*event));
            rdma_ack_cm_event(event);
            event = &event_copy;

            if (event->event == RDMA_CM_EVENT_ADDR_RESOLVED) {
                handle_addr_resolved(ctx, event->id);
            } else if (event->event == RDMA_CM_EVENT_ROUTE_RESOLVED) {
                LOG("route resolved");
                connect_or_accept(ctx, true); // connect
            } else if (event->event == RDMA_CM_EVENT_ESTABLISHED) {
                handle_conn_established(ctx);
                break;
            } else {
                std::cout << rdma_event_str(event->event) << "\n";
                die("unknown or unexpected event.");
            }
        }

        contexts.push_back(ctx);
    }

    freeaddrinfo(addr);
}

void RDMAClient::handle_addr_resolved(RDMAContext &ctx, rdma_cm_id *cm_id)
{
    assert(!connected);
    LOG("address resolved");

    if (!pds_cqs_created)
        create_pds_cqs(cm_id->verbs);

    ctx.id = cm_id;
    create_qps(ctx);

    TEST_NZ(rdma_resolve_route(cm_id, TIMEOUT_MS));
}

void RDMAClient::disconnect_all()
{
    dereg_mrs();

    for (auto &ctx: contexts)
        ctx.disconnect();

    destroy_pds_cqs();
}
