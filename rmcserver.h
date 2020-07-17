#ifndef RMC_SERVER_H
#define RMC_SERVER_H

#include "rdmaserver.h"
#include "rmc.h"

class RMCServer: public RDMAServer {

public:
    RMCServer() : RDMAServer() {}

    /* 1. post recv for id
       2. send rmc to server; wait for 1.
       3. return id */
    RMCId get_id(const RMC &rmc);
    int call(const RMCId &id);
};

#endif
