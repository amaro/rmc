#include "rmcserver.h"

int main(int argc, char* argv[])
{
    if (argc != 2)
        die("usage: server <port>");

    RMCServer server;
    server.start(atoi(argv[1]));
}
