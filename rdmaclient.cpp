#include "rdmaclient.h"

void RDMAClient::recv_buff_info()
{
    blocking_poll_one([this]() -> void {
        std::cout << "recv successful\n";
        assert(recv_msg->type == RDMAMessage::MSG_MR);
        memcpy(&peer_mr, &recv_msg->data.mr, sizeof(ibv_mr));
        std::cout << "ready for rdma ops\n";
    });
}

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
            std::cout << "route resolved\n";
            connect_or_accept(true); // connect
        } else if (event->event == RDMA_CM_EVENT_ESTABLISHED) {
            handle_conn_established(event->id);
            recv_buff_info();
            return;
        } else {
            die("unknown or unexpected event.");
        }
    }
}

void RDMAClient::handle_addr_resolved(rdma_cm_id *cm_id)
{
    assert(!connected);
    std::cout << "address resolved\n";

    create_context(id->verbs);
    this->id = cm_id;
    create_qps();
    register_client_buffers();

    struct ibv_sge sge = {
        .addr = (uintptr_t) recv_msg.get(),
        .length = sizeof(*(recv_msg.get())),
        .lkey = recv_mr->lkey
    };

    post_simple_recv(&sge);

    TEST_NZ(rdma_resolve_route(cm_id, TIMEOUT_MS));
}

void RDMAClient::register_client_buffers()
{
    recv_msg = std::make_unique<RDMAMessage>();

    rdma_buffer = std::make_unique<char[]>(RDMA_BUFF_SIZE);

    TEST_Z(recv_mr = ibv_reg_mr(pd, recv_msg.get(), sizeof(RDMAMessage),
                                                    IBV_ACCESS_LOCAL_WRITE));
    TEST_Z(rdma_buffer_mr = ibv_reg_mr(pd, rdma_buffer.get(),
        RDMA_BUFF_SIZE,
        IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ));
}

