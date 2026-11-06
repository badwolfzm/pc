/* Edge-case regression suite for the standalone time-service prototype.
 * Independent unsigned-128-bit overflow oracle; not a Warden/QEMU test. */
#include "time_service.h"
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
typedef struct {
    warden_timespec value;
    unsigned calls;
    bool fail_after_write;
} fixture;
static bool source(void *ctx, warden_clock_kind kind, warden_timespec *out) {
    fixture *f = ctx;
    (void)kind;
    ++f->calls;
    *out = f->value;
    return !f->fail_after_write;
}
static void unchanged(warden_timespec out, uint64_t uncertainty) {
    assert(out.seconds == 77 && out.nanoseconds == 88 && uncertainty == 99);
}
int main(void) {
    const int64_t sec[] = {-1, 0, 1, INT64_C(18446744072),
        INT64_C(18446744073), INT64_C(18446744074), INT64_MAX};
    const int32_t ns[] = {-1, 0, 1, 709551615, 709551616, 999999999, 1000000000};
    fixture f = {0};
    warden_time_service s;
    for (size_t i = 0; i < sizeof(sec)/sizeof(sec[0]); ++i) {
        for (size_t j = 0; j < sizeof(ns)/sizeof(ns[0]); ++j) {
            warden_time_init(&s, source, &f);
            f.value = (warden_timespec){sec[i], ns[j]};
            f.fail_after_write = false;
            warden_timespec out = {77, 88};
            uint64_t uncertainty = 99;
            warden_time_status expected;
            if (sec[i] < 0 || ns[j] < 0 || ns[j] >= 1000000000) {
                expected = WARDEN_TIME_INVALID;
            } else {
                __uint128_t value = (__uint128_t)(uint64_t)sec[i] * UINT64_C(1000000000) + (uint64_t)ns[j];
                expected = value > UINT64_MAX ? WARDEN_TIME_OVERFLOW : WARDEN_TIME_OK;
            }
            unsigned before = f.calls;
            assert(warden_time_read(&s, WARDEN_CLOCK_MONOTONIC, true,
                                    &out, &uncertainty) == expected);
            assert(f.calls == before + 1);
            if (expected == WARDEN_TIME_OK) {
                assert(out.seconds == sec[i] && out.nanoseconds == ns[j]);
                assert(uncertainty == 0 && s.monotonic_seen);
            } else {
                unchanged(out, uncertainty);
                assert(!s.monotonic_seen);
            }
        }
    }
    warden_time_init(&s, source, &f);
    warden_timespec out = {77,88};
    uint64_t uncertainty = 99;
    unsigned before = f.calls;
    assert(warden_time_read(&s, WARDEN_CLOCK_REALTIME, true, &out, &uncertainty) == WARDEN_TIME_UNAVAILABLE);
    assert(warden_time_read(&s, WARDEN_CLOCK_MONOTONIC, false, &out, &uncertainty) == WARDEN_TIME_DENIED);
    assert(warden_time_read(&s, (warden_clock_kind)99, true, &out, &uncertainty) == WARDEN_TIME_INVALID);
    assert(f.calls == before);
    unchanged(out, uncertainty);
    f.value = (warden_timespec){12,345};
    f.fail_after_write = true;
    assert(warden_time_read(&s, WARDEN_CLOCK_MONOTONIC, true, &out, &uncertainty) == WARDEN_TIME_UNAVAILABLE);
    assert(!s.monotonic_seen);
    unchanged(out, uncertainty);
    f.fail_after_write = false;
    assert(warden_time_read(&s, WARDEN_CLOCK_MONOTONIC, true, &out, &uncertainty) == WARDEN_TIME_OK);
    assert(s.monotonic_seen);
    uint64_t last = s.monotonic_last_ns;
    assert(warden_time_read(&s, WARDEN_CLOCK_MONOTONIC, true, &out, &uncertainty) == WARDEN_TIME_OK);
    assert(s.monotonic_last_ns == last);
    f.value.nanoseconds--;
    out = (warden_timespec){77,88}; uncertainty = 99;
    assert(warden_time_read(&s, WARDEN_CLOCK_MONOTONIC, true, &out, &uncertainty) == WARDEN_TIME_REGRESSION);
    assert(s.monotonic_last_ns == last);
    unchanged(out, uncertainty);
    warden_time_init(&s, source, &f);
    assert(warden_time_advance_virtual(&s, UINT64_MAX) == WARDEN_TIME_OK);
    assert(warden_time_advance_virtual(&s, 1) == WARDEN_TIME_OVERFLOW);
    assert(s.virtual_ns == UINT64_MAX);
    assert(warden_time_read(&s, WARDEN_CLOCK_VIRTUAL, true, &out, &uncertainty) == WARDEN_TIME_OK);
    assert((uint64_t)out.seconds == UINT64_MAX / UINT64_C(1000000000));
    assert((uint32_t)out.nanoseconds == UINT64_MAX % UINT64_C(1000000000));
    puts("time service boundary checks: 49 conversions plus state/denial paths passed");
    return 0;
}
