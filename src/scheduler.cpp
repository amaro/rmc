#include "scheduler.h"

HostMemory HM;

CoroRMC<int> rmc_test(int id) {
  std::cout << "start of rmcid=" << id << "\n";
  const char *buf = "deadbeef";
  co_await HM.write(buf);
  std::cout << "end of rmcid=" << id << "\n";
}

int RMCScheduler::call_rmc(const RMCId &id, CallReply &reply, size_t arg)
{
    //auto search = id_rmc_map.find(id);
    // unused for now

    // just call a hardcoded CoroRMC for now
    auto rmc1 = rmc_test(1);
    bool done = rmc1.resume();
    while (!done)
        done = rmc1.resume();

    return 0;
}
