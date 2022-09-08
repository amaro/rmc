#pragma once

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <random>
#include <string_view>

static constexpr long unsigned int RANDOM_SEED = 123;

#define TEST_NZ(x)                                             \
  do {                                                         \
    if ((x)) die("error: " #x " failed (returned non-zero)."); \
  } while (0)
#define TEST_Z(x)                                                \
  do {                                                           \
    if (!(x)) die("error: " #x " failed (returned zero/null)."); \
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

inline void __attribute__((format(printf, 1, 2))) __attribute__((noreturn))
die(const char *fmt, ...) {
  va_list arg;
  va_start(arg, fmt);
  vprintf(fmt, arg);
  va_end(arg);
  exit(1);
}

template <typename T>
inline void __attribute__((format(printf, 2, 3)))
rt_assert(T &&assertion, const char *fmt, ...) {
  if (!assertion) {
    va_list arg;
    printf("assertion failed: ");
    va_start(arg, fmt);
    vprintf(fmt, arg);
    va_end(arg);
    exit(1);
  }
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

inline void cpu_relax() {
#if defined(__aarch64__)
  asm volatile("yield" ::: "memory");
#else
  asm volatile("rep; nop" ::: "memory");
#endif
}

inline void spinloop_cycles(const long long cycles) {
  if (cycles == 0) return;

  auto start = get_cycles();

  while (get_cycles() - start < cycles) cpu_relax();
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
  if (++ref >= maxvalue) ref = 0;
}

inline void dec_with_wraparound(uint32_t &ref, const uint32_t &maxvalue) {
  /* check for underflow */
  if (--ref > maxvalue) ref = maxvalue - 1;
}

inline void set_env_var(const char *name, const char *value) {
  int ret = setenv(name, value, 1);
  rt_assert(ret == 0, "setenv() returned %d\n", ret);
  printf("ENV %s = %s\n", name, value);
}

/* https://quick-bench.com/q/BpmfGIfPh8vzrUYhSZJzzUabMTU */
template <typename Key, typename Value, std::size_t Size>
struct StaticMap {
  std::array<std::pair<Key, Value>, Size> data;
  constexpr Value at(const Key &key) const {
    const auto itr =
        std::find_if(begin(data), end(data),
                     [&key](const auto &v) { return v.first == key; });
    if (itr != end(data)) {
      return itr->second;
    } else {
      die("entry not found in StaticMap");
    }
  }
};

inline size_t hash_str(const char *s, size_t sz) {
  return std::hash<std::string_view>()(std::string_view(s, sz));
}

inline size_t hash_buff(const uint8_t *b, size_t sz) {
  return hash_str(reinterpret_cast<const char *>(b), sz);
}

template <typename T>
inline void file_to_vec(std::vector<T> &out, const char *file) {
  std::ifstream inputf(file);
  rt_assert(inputf.is_open(), "could not open input file");

  T value;
  while (inputf >> value) out.push_back(value);
}

template <typename T>
inline void shuffle_vec(std::vector<T> &vec, long unsigned int seed) {
  auto rng = std::default_random_engine{seed};
  std::shuffle(std::begin(vec) + 1, std::end(vec), rng);
}

inline void print_buffer(uint8_t *buf, size_t sz) {
  for (auto i = 0u; i < sz; i++) {
    printf("%02hhX ", buf[i]);
  }
  printf("\n");
}
