#include "time_service.h"
#include <assert.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>

typedef struct { warden_timespec monotonic, realtime; bool fail; unsigned calls; } fixture;
static bool fake_source(void *v, warden_clock_kind kind, warden_timespec *out) {
    fixture *f = v;
    f->calls++;
    if (f->fail) return false;
    *out = kind == WARDEN_CLOCK_MONOTONIC ? f->monotonic : f->realtime;
    return true;
}
static void unchanged(warden_timespec ts) {
    assert(ts.seconds == 777 && ts.nanoseconds == 999);
}
int main(void) {
    fixture f = {.monotonic = {1, 200}, .realtime = {1730000000, 250000000}};
    warden_time_service s;
    warden_time_init(&s, fake_source, &f);
    warden_timespec out = {777, 999};
    uint64_t uncertainty = 888;
    assert(warden_time_read(&s, WARDEN_CLOCK_REALTIME, true, &out, &uncertainty) == WARDEN_TIME_UNAVAILABLE);
    unchanged(out); assert(uncertainty == 888 && f.calls == 0);
    assert(warden_time_read(&s, WARDEN_CLOCK_MONOTONIC, false, &out, &uncertainty) == WARDEN_TIME_DENIED);
    unchanged(out); assert(f.calls == 0);
    assert(warden_time_read(&s, (warden_clock_kind)99, true, &out, NULL) == WARDEN_TIME_INVALID);
    assert(warden_time_read(&s, WARDEN_CLOCK_MONOTONIC, true, &out, &uncertainty) == WARDEN_TIME_OK);
    assert(out.seconds == 1 && out.nanoseconds == 200 && uncertainty == 0);
    f.monotonic.nanoseconds = 100;
    out = (warden_timespec){777,999}; uncertainty = 888;
    assert(warden_time_read(&s, WARDEN_CLOCK_MONOTONIC, true, &out, &uncertainty) == WARDEN_TIME_REGRESSION);
    unchanged(out); assert(uncertainty == 888);
    f.monotonic.nanoseconds = 300;
    assert(warden_time_read(&s, WARDEN_CLOCK_MONOTONIC, true, &out, NULL) == WARDEN_TIME_OK);
    f.monotonic.nanoseconds = 1000000000;
    out = (warden_timespec){777,999};
    assert(warden_time_read(&s, WARDEN_CLOCK_MONOTONIC, true, &out, NULL) == WARDEN_TIME_INVALID);
    unchanged(out);
    f.monotonic = (warden_timespec){INT64_MAX, 0};
    assert(warden_time_read(&s, WARDEN_CLOCK_MONOTONIC, true, &out, NULL) == WARDEN_TIME_OVERFLOW);
    unchanged(out);
    f.monotonic = (warden_timespec){2, 0};
    f.fail = true;
    assert(warden_time_read(&s, WARDEN_CLOCK_MONOTONIC, true, &out, NULL) == WARDEN_TIME_UNAVAILABLE);
    f.fail = false;
    warden_time_set_realtime_authority(&s, true, 5000000);
    assert(warden_time_read(&s, WARDEN_CLOCK_REALTIME, true, &out, &uncertainty) == WARDEN_TIME_OK);
    assert(out.seconds == 1730000000 && uncertainty == 5000000);
    warden_time_set_realtime_authority(&s, false, 0);
    out = (warden_timespec){777,999};
    assert(warden_time_read(&s, WARDEN_CLOCK_REALTIME, true, &out, NULL) == WARDEN_TIME_UNAVAILABLE);
    unchanged(out);
    assert(warden_time_advance_virtual(&s, 1000000001) == WARDEN_TIME_OK);
    assert(warden_time_read(&s, WARDEN_CLOCK_VIRTUAL, true, &out, &uncertainty) == WARDEN_TIME_OK);
    assert(out.seconds == 1 && out.nanoseconds == 1 && uncertainty == 0);
    assert(warden_time_advance_virtual(&s, UINT64_MAX) == WARDEN_TIME_OVERFLOW);
    assert(warden_time_read(NULL, WARDEN_CLOCK_VIRTUAL, true, &out, NULL) == WARDEN_TIME_INVALID);
    assert(warden_time_read(&s, WARDEN_CLOCK_VIRTUAL, true, NULL, NULL) == WARDEN_TIME_INVALID);
    puts("time service contract tests passed");
    return 0;
}
