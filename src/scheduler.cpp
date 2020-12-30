#include "scheduler.h"

CoroRMC<int> rmc_test(OneSidedClient &client, size_t num_nodes) {
    // getting a local buffer should be explicit here
    // consider cpu locality into the design
    uintptr_t base_addr = (uintptr_t) client.get_remote_base_addr();
    LLNode *node = (LLNode *) co_await client.readfromcoro(0, sizeof(LLNode));

    for (size_t i = 0; i < num_nodes - 1; ++i) {
        uint32_t offset = (uintptr_t) node->next - base_addr;
        node = (LLNode *) co_await client.readfromcoro(offset, sizeof(LLNode)); // this should take node->next vaddr
    }
}

int RMCScheduler::call_rmc(const RMCId &id, CallReply &reply, size_t arg)
{
    //auto search = id_rmc_map.find(id); unused

    // just call a hardcoded CoroRMC for now
    auto rmc1 = rmc_test(client, num_llnodes);
    int resumes = 1;

    while (!rmc1.resume()) {
        resumes++;
        client.poll_async(); // blocking
    }

    //LOG("total coro resumes = " << resumes);
    return 0;
}
