#include "hostserver.h"

void HostServer::post_recv_req()
{
    assert(rmcsready);

    struct ibv_sge sge = {
        .addr = (uintptr_t) req_buf.get(),
        .length = sizeof(*(req_buf.get())),
        .lkey = req_buf_mr->lkey
    };

    rserver.post_simple_recv(&sge);
}

void HostServer::post_send_reply()
{
    assert(rmcsready);

    struct ibv_sge sge = {
        .addr = (uintptr_t) reply_buf.get(),
        .length = sizeof(CmdReply),
        .lkey = reply_buf_mr->lkey
    };

    rserver.post_simple_send(&sge);
}

/* Compute the id for this rmc, if it doesn't exist, register it in map.
   Return the id */
void HostServer::get_rmc_id()
{
    assert(rmcsready);
    assert(req_buf->type == CmdType::GET_RMCID);

    RMC rmc(req_buf->request.getid.rmc);
    RMCId id = std::hash<RMC>{}(rmc);

    if (id_rmc_map.find(id) == id_rmc_map.end()) {
        id_rmc_map.insert({id, rmc});
        std::cout << "registered new id=" << id << "for rmc=" << rmc << "\n";
    }

    /* get_id reply */
    reply_buf->type = CmdType::GET_RMCID;
    reply_buf->reply.getid.id = id;
    post_send_reply();
    rserver.blocking_poll_nofunc(1);
}

void HostServer::call_rmc()
{
    assert(rmcsready);
    assert(req_buf->type == CmdType::CALL_RMC);

    RMCId id = req_buf->request.call.id;
    int status = 0;
    auto search = id_rmc_map.find(id);

    if (search == id_rmc_map.end()) {
        status = EINVAL;
    } else {
        std::cout << "Called RMC: " << search->second << "\n";
    }

    reply_buf->type = CmdType::CALL_RMC;
    reply_buf->reply.call.status = status;
    post_send_reply();
    rserver.blocking_poll_nofunc(1);
}

void HostServer::connect(int port)
{
    assert(!rmcsready);
    rserver.connect_events(port);

    /* nic writes incoming requests */
    req_buf_mr = rserver.register_mr(req_buf.get(), sizeof(CmdRequest), IBV_ACCESS_LOCAL_WRITE);
    /* cpu writes outgoing replies */
    reply_buf_mr = rserver.register_mr(reply_buf.get(), sizeof(CmdReply), 0);
    rmcsready = true;
}

void HostServer::handle_requests()
{
    assert(rmcsready);

    while (rmcsready) {
        post_recv_req();
        rserver.blocking_poll_nofunc(1);

        switch (req_buf->type) {
        case CmdType::GET_RMCID:
            get_rmc_id();
            break;
        case CmdType::CALL_RMC:
            call_rmc();
            break;
        case CmdType::LAST_CMD:
            disconnect();
            break;
        default:
            die("unrecognized CmdRequest type\n");
        }
    }

    std::cout << "handled request\n";
}

void HostServer::disconnect()
{
    assert(rmcsready);

    std::cout << "received disconnect req\n";
    rserver.dereg_mr(req_buf_mr);
    rserver.dereg_mr(reply_buf_mr);
    rserver.disconnect_events();
    rmcsready = false;
}

int main(int argc, char* argv[])
{
    if (argc != 2)
        die("usage: server <port>");

    HostServer server;
    server.connect(atoi(argv[1]));
    server.handle_requests();
}
