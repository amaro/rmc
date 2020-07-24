#ifndef HOST_SERVER_H
#define HOST_SERVER_H

#include <cstdlib>
#include "rdmaserver.h"
#include "rmc.h"

#define PAGE_SIZE       4096
#define MEMORY_SIZE     (1 << 20) // 1MB

class HostServer {

    RDMAServer rserver;
    /* rmc server ready */
    bool hsready;
    void *memory;
    ibv_mr *memory_mr;

public:
    HostServer() : hsready(false) {
        memory = aligned_alloc(PAGE_SIZE, MEMORY_SIZE);
    }

    ~HostServer() {
        free(memory);
    }

    void connect_and_block(int port);

    void disconnect();
};

#endif
