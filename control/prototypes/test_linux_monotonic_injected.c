#define _POSIX_C_SOURCE 200809L
#include "linux_monotonic_source.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <time.h>

/* Injected clock_gettime replacement (compile with -Dclock_gettime=
 * warden_mock_clock_gettime). Tests real adapter error branches. */
static int mode, calls;
int warden_mock_clock_gettime(clockid_t id, struct timespec *ts) {
    ++calls;
    assert(id == CLOCK_MONOTONIC);
    if (mode == 0) { errno = EIO; return -1; }
    ts->tv_sec = mode == 2 ? -1 : 123;
    ts->tv_nsec = mode == 3 ? -1 : mode == 4 ? 1000000000L : 456;
    return 0;
}

int main(void) {
    for (mode = 0; mode <= 4; ++mode) {
        calls = 0;
        warden_timespec out = {77, 88};
        bool ok = warden_linux_monotonic_source(NULL, WARDEN_CLOCK_MONOTONIC, &out);
        assert(ok == (mode == 1));
        assert(calls == 1);
        if (ok) assert(out.seconds == 123 && out.nanoseconds == 456);
        else assert(out.seconds == 77 && out.nanoseconds == 88);
    }
    calls = 0;
    warden_timespec out = {77, 88};
    assert(!warden_linux_monotonic_source(NULL, WARDEN_CLOCK_REALTIME, &out));
    assert(!warden_linux_monotonic_source(NULL, WARDEN_CLOCK_MONOTONIC, NULL));
    assert(calls == 0 && out.seconds == 77 && out.nanoseconds == 88);
    puts("linux monotonic injected-error tests passed");
    return 0;
}
