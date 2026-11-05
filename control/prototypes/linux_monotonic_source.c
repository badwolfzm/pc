#define _POSIX_C_SOURCE 200809L
#include "linux_monotonic_source.h"
#include <limits.h>
#include <time.h>

bool warden_linux_monotonic_source(void *unused, warden_clock_kind kind,
                                   warden_timespec *out) {
    (void)unused;
    if (!out || kind != WARDEN_CLOCK_MONOTONIC) return false;
#if defined(__linux__) && defined(CLOCK_MONOTONIC)
    struct timespec reading;
    if (clock_gettime(CLOCK_MONOTONIC, &reading) != 0) return false;
    if (reading.tv_sec < 0 || (uintmax_t)reading.tv_sec > (uintmax_t)INT64_MAX ||
        reading.tv_nsec < 0 || reading.tv_nsec >= 1000000000L) return false;
    warden_timespec candidate = {(int64_t)reading.tv_sec, (int32_t)reading.tv_nsec};
    *out = candidate;
    return true;
#else
    return false;
#endif
}
