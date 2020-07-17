#include "rmcserver.h"

int main(int argc, char* argv[])
{
    if (argc != 2)
        die("usage: server <port>");

    RMCServer server;
    server.connect_events(atoi(argv[1]));

    server.disconnect_events();
}
