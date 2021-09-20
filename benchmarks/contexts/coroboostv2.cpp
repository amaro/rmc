#include "utils/utils.h"
#include <boost/coroutine2/all.hpp>
#include <chrono>
#include <iostream>

using namespace boost::coroutines2;

const int NUM_REPS = 1000000;

void yield_bench(coroutine<void>::push_type &sink) {
  for (int i = 0; i < NUM_REPS / 2; ++i) {
    sink();
  }
}

int main() {
  coroutine<void>::pull_type source{yield_bench};
  time_point start;
  long long duration;

  start = time_start();
  for (int i = 0; i < NUM_REPS / 2; ++i) {
    source();
  }
  duration = time_end(start);

  std::cout << "duration=" << duration << " ns\n";
  std::cout << "per yield=" << duration / (double)NUM_REPS << " ns\n";
}
