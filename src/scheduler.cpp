#include "scheduler.h"

CoroRMC<int> rmc_test(OneSidedClient &client) {
    std::cout << "start of rmc\n";
    auto a = client.readfromcoro(0, 16);
    //co_await client.readfromcoro(0, 16);
    std::cout << "print after readfromcoro\n";
    Node *node = co_await a; /// node -> local rdma memory region that we just read
    node->next
    std::cout << "end of rmcid\n";
}

int RMCScheduler::call_rmc(const RMCId &id, CallReply &reply, size_t arg)
{
    //auto search = id_rmc_map.find(id);
    // unused for now

    // just call a hardcoded CoroRMC for now
    auto rmc1 = rmc_test(client);
    int resumes = 0;

    while (!rmc1.resume()) {
        std::cout << "after resume " << ++resumes << "\n";
        client.poll_async(); // blocking
    }

    return 0;
}
