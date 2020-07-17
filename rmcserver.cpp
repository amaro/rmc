#include "rmcserver.h"

void RMCServer::start(int port) {
    connect_events(port);

	disconnect_events();
}
