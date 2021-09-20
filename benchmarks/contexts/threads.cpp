#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>

namespace {

using ns = std::chrono::duration<double, std::nano>;
constexpr int kMeasureRounds = 1000000;

void BenchYield() {
  auto th = std::thread([]() {
    for (int i = 0; i < kMeasureRounds / 2; ++i)
      std::this_thread::yield();
  });

  for (int i = 0; i < kMeasureRounds / 2; ++i)
    std::this_thread::yield();

  th.join();
}

void PrintResult(std::string name, ns time) {
  time /= kMeasureRounds;
  std::cout << "test '" << name << "' took " << time.count() << " ns."
            << std::endl;
}

void MainHandler(void *arg) {
  auto start = std::chrono::steady_clock::now();
  auto finish = std::chrono::steady_clock::now();

  start = std::chrono::steady_clock::now();
  BenchYield();
  finish = std::chrono::steady_clock::now();
  PrintResult("Yield", std::chrono::duration_cast<ns>(finish - start));
}

} // anonymous namespace

int main(int argc, char *argv[]) {
  MainHandler(NULL);
  return 0;
}
