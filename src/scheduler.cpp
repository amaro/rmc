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

void RMCScheduler::add_rmc()
{
    CoroRMC<int> *rmc = new auto(rmc_test(client, num_llnodes));
    runqueue.push(rmc);
    std::cout << "added rmc=" << std::hex << rmc << "\n";
}

int RMCScheduler::call_rmc(const RMCId &id, CallReply &reply, size_t arg)
{
    //auto search = id_rmc_map.find(id); unused
    int resumes = 1;
    int completed = 0;
    std::cout << "call_rmc\n";

    while (!runqueue.empty() || !memqueue.empty()) {
        /* if there's an RMC to run, run it */
        if (!runqueue.empty()) {
            CoroRMC<int> *rmc = runqueue.front();
            runqueue.pop();

            resumes++;
            /* if RMC is not done running, add it to memqueue */
            std::cout << "will run rmc=" << std::hex << rmc << "\n";
            if (!rmc->resume()) {
                memqueue.push(rmc);
            } else {
                std::cout << "finished rmc=" << std::hex << rmc << "\n";
            }
        }

        /* poll host memory qp */
        completed = client.poll_async(); // not blocking
        std::cout << "completed=" << completed << "\n";
        for (int i = 0; i < completed; ++i) {
            runqueue.push(memqueue.front());
            memqueue.pop();
        }
    }

    LOG("resumes=" << std::dec << resumes);
    return 0;
}
