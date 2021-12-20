#include <stdint.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#include <time.h>
#endif

#ifdef __APPLE__
#include <mach/mach_time.h>
#endif

#include "dr_time.h"

int64_t dr_clock_time() {
#if defined(_WIN32)
    FILETIME ft;
    GetSystemTimePreciseAsFileTime(&ft);
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    int64_t ts = u.QuadPart;
    return (ts - 116444736000000000) * 100;
#elif defined(CLOCK_REALTIME)
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ((int64_t)ts.tv_sec * 1e9L) + ts.tv_nsec;
#else
    struct timeval tv;
    gettimeofday(&tv);
    return ((int64_t)tv.tv_sec * 1e9L) + (tv.tv_usec * 1e3L);
#endif
}

// int64 monotonic time (in nanoseconds)
int64_t dr_monotonic_time() {
#if defined(__APPLE__)
    static int64_t mul = 1, div = 0;
    if (! div) {
        mach_timebase_info_data_t info = {0};
        mach_timebase_info(&info);
        mul = info.numer;
        div = info.denom;
    }
    int64_t ticks = mach_absolute_time();
    return (ticks / div) * mul + (ticks % div) * mul / div;
#elif defined(_WIN32)
    static int64_t mul = 1, div = 0;
    if (! div) {
        LARGE_INTEGER freq;
        if (! QueryPerformanceFrequency(&freq)) {
            return -1;
        }
        mul = 1e9L;
        div = freq.QuadPart;
    }
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    int64_t ticks = now.QuadPart;
    return (ticks / div) * mul + (ticks % div) * mul / div;
#elif defined(CLOCK_MONOTONIC)
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((int64_t)ts.tv_sec * 1e9L) + ts.tv_nsec;
#else
    // no support for monotonic clock down here
    struct timeval tv;
    gettimeofday(&tv);
    return ((int64_t)tv.tv_sec * 1e9L) + (tv.tv_usec * 1e3L);
#endif
}

// offset between monotonic and real time clock (RTC - monotonic)
int64_t dr_monotonic_offset() {
    return dr_monotonic_time() - dr_clock_time();
}
