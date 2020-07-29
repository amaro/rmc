#ifndef UTILS_H
#define UTILS_H

#include <chrono>
typedef std::chrono::time_point<std::chrono::steady_clock> time_point;

inline void die(const std::string& msg)
{
    std::cerr << msg << std::endl;
    exit(1);
}

#define TEST_NZ(x) do { if ( (x)) die("error: " #x " failed (returned non-zero)." ); } while (0)
#define TEST_Z(x)  do { if (!(x)) die("error: " #x " failed (returned zero/null)."); } while (0)

#endif
