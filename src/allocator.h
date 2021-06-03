#include <forward_list>
#include <utils/utils.h>

#ifndef ALLOCATOR_H
#define ALLOCATOR_H

namespace RMCAllocator {
    /* 128 bytes */
    struct PromiseAllocNode {
        uint64_t pad[16];
    };

    /* 16 bytes */
    struct RMCAllocNode {
        uint64_t pad[2];
    };

    static constexpr size_t NUM_NODES = 1024;

    inline std::forward_list<PromiseAllocNode *> promise_list;
    inline std::forward_list<RMCAllocNode *> rmc_list;

    inline void init() {
        static_assert(sizeof(PromiseAllocNode) == 128);
        static_assert(sizeof(RMCAllocNode) == 16);

        for (auto i = 0u; i < NUM_NODES; ++i) {
           promise_list.push_front(new PromiseAllocNode());
           rmc_list.push_front(new RMCAllocNode());
        }
    }

    inline void release() {
        for (auto i = 0u; i < NUM_NODES; ++i) {
           delete promise_list.front();
           delete rmc_list.front();
           promise_list.pop_front();
           rmc_list.pop_front();
        }
    }

    inline void *get_promise() {
        if (promise_list.empty())
            DIE("promise_list is empty");

        PromiseAllocNode *promise = promise_list.front();
        promise_list.pop_front();
        return promise;
    }

    inline void delete_promise(void *ptr) {
        PromiseAllocNode *promise = reinterpret_cast<PromiseAllocNode *>(ptr);
        promise_list.push_front(promise);
    }

    inline void *get_rmc() {
        if (rmc_list.empty())
            DIE("rmc_list is empty");

        RMCAllocNode *rmc = rmc_list.front();
        rmc_list.pop_front();
        return rmc;
    }

    inline void delete_rmc(void *ptr) {
        RMCAllocNode *rmc = reinterpret_cast<RMCAllocNode *>(ptr);
        rmc_list.push_front(rmc);
    }
};

#endif
