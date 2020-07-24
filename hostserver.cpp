#include "hostserver.h"

void HostServer::connect_and_block(int port)
{
    assert(!hsready);
    rserver.connect_events(port);

    memory_mr = rserver.register_mr(memory, MEMORY_SIZE,
            IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_LOCAL_WRITE);

    hsready = true;

    rserver.disconnect_events();
}

void HostServer::disconnect()
{
    assert(hsready);

    std::cout << "received disconnect req\n";
    rserver.disconnect_events();
    hsready = false;
}

int main(int argc, char* argv[])
{
    if (argc != 2)
        die("usage: server <port>");

    HostServer server;
    server.connect_and_block(atoi(argv[1]));
}
