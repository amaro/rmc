#ifndef ALLOCATOR_H
#define ALLOCATOR_H

#include <forward_list>

#include "utils/utils.h"
#include "rdma/rdmapeer.h"

namespace RMCAllocator {
    /* 112 bytes */
    struct PromiseAllocNode {
        uint64_t pad[14];
    };

    static constexpr size_t NUM_NODES = QP_MAX_2SIDED_WRS;

    inline std::forward_list<PromiseAllocNode *> promise_list;

    inline void init() {
        static_assert(sizeof(PromiseAllocNode) == 112);

        for (auto i = 0u; i < NUM_NODES; ++i) {
           promise_list.push_front(new PromiseAllocNode());
        }
    }

    inline void release() {
        for (auto i = 0u; i < NUM_NODES; ++i) {
           delete promise_list.front();
           promise_list.pop_front();
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
};

#endif
