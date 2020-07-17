#ifndef RMC_CLIENT_H
#define RMC_CLIENT_H

#include "rdmaclient.h"
#include "rmc.h"

class RMCClient: public RDMAClient {

public:
    RMCClient() : RDMAClient() {}

    /* gets the id of an RMC.
       if it exists, simply returns the id;
       if it doesn't exist, creates it and returns the id */
    RMCId get_id(const RMC &rmc);

    /* calls an RMC by its id.
       TODO: figure out params, returns, etc. */
    void call(const RMCId &id);
};

#endif
