#include "rmcserver.h"

void RMCServer::listen(int port)
{
    assert(!rmcsready);
    rserver.connect_events(port);

    req_buf_mr = rserver.register_mr(req_buf.get(), sizeof(RMCRequest), 0);
    reply_buf_mr = rserver.register_mr(reply_buf.get(), sizeof(RMCReply), IBV_ACCESS_LOCAL_WRITE);
    rmcsready = true;
}

RMCId RMCServer::get_id(const RMC &rmc)
{
    assert(rmcsready);
    return 0;
}

int RMCServer::call(const RMCId &id)
{
    assert(rmcsready);
    return 0;
}
