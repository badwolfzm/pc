#include "linux_monotonic_source.h"
#include <assert.h>
#include <stdio.h>
#include <stdint.h>

int main(void) {
    warden_time_service service;
    warden_time_init(&service, warden_linux_monotonic_source, NULL);
    warden_timespec out = {123, 456};
    uint64_t error_ns = 789;
    assert(!warden_linux_monotonic_source(NULL, WARDEN_CLOCK_REALTIME, &out));
    assert(!warden_linux_monotonic_source(NULL, WARDEN_CLOCK_VIRTUAL, &out));
    assert(!warden_linux_monotonic_source(NULL, WARDEN_CLOCK_MONOTONIC, NULL));
    assert(out.seconds == 123 && out.nanoseconds == 456);
    assert(warden_time_read(&service, WARDEN_CLOCK_REALTIME, true, &out, &error_ns) == WARDEN_TIME_UNAVAILABLE);
    assert(out.seconds == 123 && out.nanoseconds == 456 && error_ns == 789);
    assert(warden_time_read(&service, WARDEN_CLOCK_MONOTONIC, false, &out, &error_ns) == WARDEN_TIME_DENIED);
    assert(out.seconds == 123 && out.nanoseconds == 456 && error_ns == 789);
#if defined(__linux__)
    assert(warden_time_read(&service, WARDEN_CLOCK_MONOTONIC, true, &out, &error_ns) == WARDEN_TIME_OK);
    assert(out.seconds >= 0 && out.nanoseconds >= 0 && out.nanoseconds < 1000000000);
    assert(error_ns == 0);
    warden_timespec first = out;
    for (unsigned i = 0; i < 1000; ++i) {
        assert(warden_time_read(&service, WARDEN_CLOCK_MONOTONIC, true, &out, NULL) == WARDEN_TIME_OK);
        assert(out.seconds > first.seconds || (out.seconds == first.seconds && out.nanoseconds >= first.nanoseconds));
        first = out;
    }
#else
    assert(warden_time_read(&service, WARDEN_CLOCK_MONOTONIC, true, &out, &error_ns) == WARDEN_TIME_UNAVAILABLE);
#endif
    /* Even when explicitly permitted, adapter refuses to claim trusted UTC. */
    warden_time_set_realtime_authority(&service, true, 1);
    out = (warden_timespec){123,456}; error_ns = 789;
    assert(warden_time_read(&service, WARDEN_CLOCK_REALTIME, true, &out, &error_ns) == WARDEN_TIME_UNAVAILABLE);
    assert(out.seconds == 123 && out.nanoseconds == 456 && error_ns == 789);
    puts("Linux monotonic-clock adapter: all assertions passed");
    return 0;
}
