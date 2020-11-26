#include "rdmaserver.h"

void RDMAServer::connect_events(int port)
{
    sockaddr_in addr = {};
    rdma_cm_event *event = nullptr;
    event_channel = nullptr;

    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    TEST_Z(event_channel = rdma_create_event_channel());
    TEST_NZ(rdma_create_id(event_channel, &listen_id, nullptr, RDMA_PS_TCP));
    TEST_NZ(rdma_bind_addr(listen_id, (sockaddr *) &addr));
    TEST_NZ(rdma_listen(listen_id, 1));

    LOG("listening on port: " << port);

    while (rdma_get_cm_event(event_channel, &event) == 0) {
        bool should_break = false;

        if (event->event == RDMA_CM_EVENT_CONNECT_REQUEST) {
            handle_conn_request(event->id);
        } else if (event->event == RDMA_CM_EVENT_ESTABLISHED) {
            handle_conn_established(event->id);
            should_break = true;
        } else {
            die("unknown or unexpected event at connect_events().");
        }

		rdma_ack_cm_event(event);

        if (should_break)
            break;
    }
}

void RDMAServer::disconnect_events()
{
    rdma_cm_event *event = nullptr;

    while (rdma_get_cm_event(event_channel, &event) == 0) {
        switch (event->event) {
		case RDMA_CM_EVENT_DISCONNECTED:
		    rdma_ack_cm_event(event);
			disconnect();
            return;
		default:
            die("unknown or unexpected event at disconnect_events().");
        }
    }
}

void RDMAServer::handle_conn_request(rdma_cm_id *cm_id)
{
    assert(!connected);
    LOG("connect request");

    create_context(cm_id->verbs);
    this->id = cm_id;
    create_qps();

    connect_or_accept(false); // accept
}