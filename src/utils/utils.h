#ifndef UTILS_H
#define UTILS_H

#include <chrono>
#include <cstring>
#include "logger.h"

#define TEST_NZ(x) do { if ( (x)) die("error: " #x " failed (returned non-zero)." ); } while (0)
#define TEST_Z(x)  do { if (!(x)) die("error: " #x " failed (returned zero/null)."); } while (0)

typedef std::chrono::time_point<std::chrono::steady_clock> time_point;

inline time_point time_start()
{
    return std::chrono::steady_clock::now();
}

inline long long time_end(const time_point &start)
{
    time_point end = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count();
}

inline void die(const std::string& msg)
{
    LOG(msg);
    exit(1);
}

template <typename T>
inline void num_to_str(const T &data, char *dst, size_t count)
{
    std::string str = std::to_string(data);
    std::strncpy(dst, str.c_str(), count);
}

inline long long get_cycles()
{
#if defined(__aarch64__)
    long long virtual_timer_value;
    asm volatile("mrs %0, cntvct_el0" : "=r"(virtual_timer_value));
    return virtual_timer_value;
#else
    return 0;
#endif
}

inline long long cycles_to_us(long long cycles, long long freq)
{
#if defined(__aarch64__)
    return cycles * 1000000000 / freq;
#else
    return 0;
#endif
}

inline long long get_freq()
{
#if defined(__aarch64__)
    long long freq;
    asm volatile("mrs %0, cntfrq_el0" : "=r" (freq));
    return freq;
#else
    return 0;
#endif
}

#endif
