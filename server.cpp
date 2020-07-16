#include "server.h"

void RDMAServer::send_buff_info()
{
    struct ibv_sge sge = {};
    assert(connected);

    send_msg->type = Message::MSG_MR;
    memcpy(&send_msg->data.mr, rdma_buffer_mr, sizeof(struct ibv_mr));

    sge.addr = (uintptr_t) send_msg.get();
    sge.length = sizeof(Message);
    sge.lkey = send_mr->lkey;

    post_simple_send(&sge);

    blocking_poll_one([]() -> void {
        std::cout << "send successful\n";
    });
}

void RDMAServer::connect_from_client(int port)
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
        struct rdma_cm_event event_copy;

        memcpy(&event_copy, event, sizeof(*event));
        rdma_ack_cm_event(event);
        event = &event_copy;

        if (event->event == RDMA_CM_EVENT_CONNECT_REQUEST) {
            handle_conn_request(event->id);
        } else if (event->event == RDMA_CM_EVENT_ESTABLISHED) {
            handle_conn_established(event->id);
            send_buff_info();
        } else if (event->event == RDMA_CM_EVENT_DISCONNECTED) {
            disconnect();
            break;
        } else {
            die("unknown or unexpected event.");
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
    register_server_buffers();

    connect_or_accept(false); // accept
}

void RDMAServer::register_server_buffers()
{
    send_msg = std::make_unique<Message>();

    rdma_buffer = std::make_unique<char[]>(RDMA_BUFF_SIZE);

    TEST_Z(send_mr = ibv_reg_mr(pd, send_msg.get(), sizeof(Message), 0));
    TEST_Z(rdma_buffer_mr = ibv_reg_mr(pd, rdma_buffer.get(),
        RDMA_BUFF_SIZE,
        IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ));
}
