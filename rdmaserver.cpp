#include "rdmaserver.h"

void RDMAServer::send_buff_info()
{
    struct ibv_sge sge = {};
    assert(connected);

    send_msg->type = RDMAMessage::MSG_MR;
    memcpy(&send_msg->data.mr, rdma_buffer_mr, sizeof(struct ibv_mr));

    sge.addr = (uintptr_t) send_msg.get();
    sge.length = sizeof(RDMAMessage);
    sge.lkey = send_mr->lkey;

    post_simple_send(&sge);

    blocking_poll_one([]() -> void {
        std::cout << "send successful\n";
    });
}

void RDMAServer::connect_events(int port)
{
    sockaddr_in addr = {};
    rdma_cm_event *event = nullptr;
    rdma_cm_id *listener = nullptr;
    event_channel = nullptr;

    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    TEST_Z(event_channel = rdma_create_event_channel());
    TEST_NZ(rdma_create_id(event_channel, &listener, nullptr, RDMA_PS_TCP));
    TEST_NZ(rdma_bind_addr(listener, (sockaddr *) &addr));
    TEST_NZ(rdma_listen(listener, 1));

    std::cout << "listening on port: " << port << "\n";

    while (rdma_get_cm_event(event_channel, &event) == 0) {
        bool should_break = false;

        if (event->event == RDMA_CM_EVENT_CONNECT_REQUEST) {
            handle_conn_request(event->id);
        } else if (event->event == RDMA_CM_EVENT_ESTABLISHED) {
            handle_conn_established(event->id);
            send_buff_info();
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
    std::cout << "connect request \n";

    create_context(cm_id->verbs);
    this->id = cm_id;
    create_qps();
    register_server_mrs();

    connect_or_accept(false); // accept
}

void RDMAServer::register_server_mrs()
{
    send_mr = register_mr(send_msg.get(), sizeof(RDMAMessage), 0);
    rdma_buffer_mr = register_mr(rdma_buffer.get(), RDMA_BUFF_SIZE,
         IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ);
}
