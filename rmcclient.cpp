#include "rmcclient.h"

void RMCClient::connect(const std::string &ip, const std::string &port)
{
    assert(!rmccready);
    rclient.connect_to_server(ip, port);

    req_buf_mr = rclient.register_mr(req_buf.get(), sizeof(RMCRequest), 0);
    reply_buf_mr = rclient.register_mr(reply_buf.get(), sizeof(RMCReply), IBV_ACCESS_LOCAL_WRITE);
    rmccready = true;
}

RMCId RMCClient::get_id(const RMC &rmc)
{
    assert(rmccready);

    return 0;
}

int RMCClient::call(const RMCId &id)
{
    assert(rmccready);
    return 0;
}
