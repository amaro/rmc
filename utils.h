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

#endif
