#include "rdmaserver.h"

void RDMAServer::connect_from_client(int port)
{
    sockaddr_in addr = {};
    rdma_cm_event *event = nullptr;
    rdma_cm_id *listener = nullptr;

    addr.sin_family = AF_INET;

    for (auto qp = 0u; qp < this->num_qps; ++qp) {
        RDMAContext ctx(qp);

        addr.sin_port = htons(port + qp);

        TEST_Z(ctx.event_channel = rdma_create_event_channel());
        TEST_NZ(rdma_create_id(ctx.event_channel, &listener, nullptr, RDMA_PS_TCP));
        TEST_NZ(rdma_bind_addr(listener, (sockaddr *) &addr));
        TEST_NZ(rdma_listen(listener, 1));

        LOG("listening on port: " << port);

        while (rdma_get_cm_event(ctx.event_channel, &event) == 0) {
            bool should_break = false;

            if (event->event == RDMA_CM_EVENT_CONNECT_REQUEST) {
                handle_conn_request(ctx, event->id);
            } else if (event->event == RDMA_CM_EVENT_ESTABLISHED) {
                handle_conn_established(ctx);
                should_break = true;
            } else {
                die("unknown or unexpected event at connect_from_client().");
            }

            rdma_ack_cm_event(event);

            if (should_break)
                break;
        }

        contexts.push_back(ctx);
    }
}

void RDMAServer::disconnect_events()
{
    rdma_cm_event *event = nullptr;

    for (auto &ctx: contexts) {
        while (rdma_get_cm_event(ctx.event_channel, &event) == 0) {
            struct rdma_cm_event event_copy;

            memcpy(&event_copy, event, sizeof(*event));
            rdma_ack_cm_event(event);
            event = &event_copy;

            switch (event->event) {
            case RDMA_CM_EVENT_DISCONNECTED:
                ctx.disconnect();
                return;
            default:
                die("unknown or unexpected event at disconnect_events().");
            }
        }
    }

    dereg_mrs();
    destroy_pds_cqs();
}

void RDMAServer::handle_conn_request(RDMAContext &ctx, rdma_cm_id *cm_id)
{
    assert(!ctx.connected);
    LOG("connect request");

    if (!pds_cqs_created)
        create_pds_cqs(cm_id->verbs);

    ctx.cm_id = cm_id;
    create_qps(ctx);

    connect_or_accept(ctx, false); // accept
}
