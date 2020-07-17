#ifndef RMC_SERVER_H
#define RMC_SERVER_H

#include "rdmaserver.h"
#include "rmc.h"

class RMCServer: public RDMAServer {

public:
    RMCServer() : RDMAServer() {}

    void start(int port);
};

#endif
