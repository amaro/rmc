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

void RMCScheduler::create_rmc()
{
    static int id = 0;
    CoroRMC<int> *rmc = new auto(rmc_test(client, num_llnodes));
    rmc->id = id++;
    runqueue.push(rmc);
    std::cout << "created new rmc=" << rmc->id << "\n";
}

bool RMCScheduler::schedule()
{
    //auto search = id_rmc_map.find(id); unused
    int completed = 0;
    bool finished_rmc = false;

    /* if there's an RMC ready to run, run it */
    while (!runqueue.empty()) {
        CoroRMC<int> *rmc = runqueue.front();
        runqueue.pop();

        /* if RMC is not done running, add it to memqueue */
        std::cout << "executing rmc=" << rmc->id << "\n";
        if (!rmc->resume()) {
            memqueue.push(rmc);
        } else {
            finished_rmc = true;
            std::cout << "finished rmc=" << rmc->id << "\n";
        }
    }

    /* if there are RMCs waiting for their host mem accesses to finish,
       poll their qp */
    if (!memqueue.empty()) {
        completed = client.poll_async(); // not blocking
        std::cout << "hostmem accesses completed=" << completed << "\n";
        for (int i = 0; i < completed; ++i) {
            std::cout << "adding to runqueue rmc=" << memqueue.front()->id << "\n";
            runqueue.push(memqueue.front());
            memqueue.pop();
        }
    }

    return finished_rmc;
}
