#include "server.h"

int main(int argc, char* argv[])
{
    if (argc != 2)
        die("usage: server <port>");

    RDMAServer server;
    server.connect_from_client(atoi(argv[1]));
}
