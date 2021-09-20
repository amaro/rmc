#include "utils/utils.h"
#include <chrono>
#include <iostream>

const int NUM_REPS = 1000000;

int __attribute__((noinline)) func() {
  asm("");
  return 1;
}

int main() {
  time_point start;
  long long duration;

  start = time_start();
  for (int i = 0; i < NUM_REPS / 2; ++i) {
    func();
  }
  duration = time_end(start);

  std::cout << "duration=" << duration << " ns\n";
  std::cout << "per yield=" << duration / (double)NUM_REPS << " ns\n";
}
