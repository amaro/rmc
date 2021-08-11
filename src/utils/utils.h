#pragma once

#include "logger.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <random>

static constexpr int RANDOM_SEED = 123;

#define TEST_NZ(x)                                                             \
  do {                                                                         \
    if ((x))                                                                   \
      die("error: " #x " failed (returned non-zero).");                        \
  } while (0)
#define TEST_Z(x)                                                              \
  do {                                                                         \
    if (!(x))                                                                  \
      die("error: " #x " failed (returned zero/null).");                       \
  } while (0)

typedef std::chrono::time_point<std::chrono::steady_clock> time_point;

inline time_point time_start() { return std::chrono::steady_clock::now(); }

inline long long time_end(const time_point &start, const time_point &end) {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(end - start)
      .count();
}

inline long long time_end(const time_point &start) {
  const time_point end = std::chrono::steady_clock::now();
  return time_end(start, end);
}

inline void die(const std::string &msg) {
  LOG(msg);
  exit(1);
}

template <typename T>
inline void num_to_str(const T &data, char *dst, size_t count) {
  const std::string str = std::to_string(data);
  std::strncpy(dst, str.c_str(), count);
}

inline long long get_cycles() {
#if defined(__aarch64__)
  long long virtual_timer_value;
  asm volatile("mrs %0, cntvct_el0" : "=r"(virtual_timer_value));
  return virtual_timer_value;
#else
  uint64_t rax;
  uint64_t rdx;
  asm volatile("rdtsc" : "=a"(rax), "=d"(rdx));
  return static_cast<long long>((rdx << 32) | rax);
#endif
}

inline long long cycles_to_ns(long long cycles, long long freq) {
  return cycles * 1000000000 / freq;
}

inline long long ns_to_cycles(long long ns, long long freq) {
  double cycles = ns * freq * 0.000000001;
  return static_cast<long long>(cycles);
}

inline long long get_freq() {
#if defined(__aarch64__)
  long long freq;
  asm volatile("mrs %0, cntfrq_el0" : "=r"(freq));
  return freq;
#else
  /* from https://github.com/erpc-io/eRPC/blob/master/src/util/timer.h */
  time_point start = time_start();
  const uint64_t rdtsc_start = get_cycles();

  // Do not change this loop! The hardcoded value below depends on this loop
  // and prevents it from being optimized out.
  uint64_t sum = 5;
  for (uint64_t i = 0; i < 1000000; i++) {
    sum += i + (sum + i) * (i % sum);
  }
  TEST_Z(sum == 13580802877818827968ull);

  const uint64_t rdtsc_cycles = get_cycles() - rdtsc_start;
  const long long time_ns = time_end(start);
  const double freq =
      rdtsc_cycles / static_cast<double>(time_ns) * 1000000000.0;
  return static_cast<long long>(freq);
#endif
}

template <typename T>
inline void inc_with_wraparound(T &ref, const T &maxvalue) {
  if (++ref >= maxvalue)
    ref = 0;
}

inline void dec_with_wraparound(uint32_t &ref, const uint32_t &maxvalue) {
  /* check for underflow */
  if (--ref > maxvalue)
    ref = maxvalue - 1;
}

/* creates a randomized linked list over an already allocated *buffer */
template <typename T>
inline T *create_linkedlist(void *buffer, size_t bufsize) {
  size_t num_nodes = bufsize / sizeof(T);
  std::vector<T *> indices(num_nodes);
  T *linkedlist = new (buffer) T[num_nodes];

  for (auto i = 0u; i < num_nodes; ++i)
    indices[i] = &linkedlist[i];

  auto rng = std::default_random_engine{RANDOM_SEED};
  std::shuffle(std::begin(indices) + 1, std::end(indices), rng);

  for (auto i = 0u; i < num_nodes; ++i) {
    T *cur = indices[i];
    if (i < num_nodes - 1)
      cur->next = indices[i + 1];
    else
      cur->next = indices[0];

    cur->data = 1;
  }

  LOG("linkedlist[0]=" << linkedlist);
  LOG("linkedlist[0].next=" << linkedlist[0].next);
  return linkedlist;
}

/* stackoverflow.com/questions/8918791/how-to-properly-free-the-memory-allocated-by-placement-new
 */
template <typename T> inline void destroy_linkedlist(T *linkedlist) {
  linkedlist->~T();
}
