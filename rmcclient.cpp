#include "rmcclient.h"

/* post a recv for RMCReply */
void RMCClient::post_recv_reply()
{
    assert(rmccready);

    struct ibv_sge sge = {
        .addr = (uintptr_t) reply_buf.get(),
        .length = sizeof(*(reply_buf.get())),
        .lkey = reply_buf_mr->lkey
    };

    rclient.post_simple_recv(&sge);
}

void RMCClient::post_send_req()
{
    assert(rmccready);

    struct ibv_sge sge = {
        .addr = (uintptr_t) req_buf.get(),
        .length = sizeof(RMCRequest),
        .lkey = req_buf_mr->lkey
    };

    rclient.post_simple_send(&sge);
}

void RMCClient::connect(const std::string &ip, const std::string &port)
{
    assert(!rmccready);
    rclient.connect_to_server(ip, port);

    req_buf_mr = rclient.register_mr(req_buf.get(), sizeof(RMCRequest), 0);
    reply_buf_mr = rclient.register_mr(reply_buf.get(), sizeof(RMCReply), IBV_ACCESS_LOCAL_WRITE);
    rmccready = true;
}

RMCId RMCClient::get_rmc_id(const RMC &rmc)
{
    assert(rmccready);

    post_recv_reply();

    /* get_id request */
    req_buf->type = RMCType::RMC_GET_ID;
    rmc.copy(req_buf->request.getid.rmc, sizeof(req_buf->request.getid.rmc));
    post_send_req();

    /* poll twice, one for send, one for recv */
    rclient.blocking_poll_nofunc(2);

    /* read RMCReply */
    assert(reply_buf->type == RMCType::RMC_GET_ID);
    return reply_buf->reply.getid.id;
}

int RMCClient::call_rmc(const RMCId &id)
{
    assert(rmccready);

    post_recv_reply();

    /* call request */
    req_buf->type = RMCType::RMC_CALL;
    req_buf->request.call.id = id;
    post_send_req();

    /* poll twice, one for send, one for recv */
    rclient.blocking_poll_nofunc(2);

    /* read RMCReply */
    assert(reply_buf->type == RMCType::RMC_CALL);
    return reply_buf->reply.call.status;
}

void RMCClient::last_cmd()
{
    assert(rmccready);

    req_buf->type = RMCType::RMC_LAST;
    post_send_req();

    /* poll once for send */
    rclient.blocking_poll_nofunc(1);

    disconnect();
}

void RMCClient::disconnect()
{
    assert(rmccready);
    rclient.dereg_mr(reply_buf_mr);
    rclient.dereg_mr(req_buf_mr);
    rclient.disconnect();
    rmccready = false;
}
