#include "time_service.h"
#include <limits.h>
#include <stddef.h>

#define NS_PER_SECOND UINT64_C(1000000000)

static bool valid_spec(warden_timespec ts) {
    return ts.seconds >= 0 && ts.nanoseconds >= 0 &&
           ts.nanoseconds < (int32_t)NS_PER_SECOND;
}

static uint64_t to_ns(warden_timespec ts) {
    return (uint64_t)ts.seconds * NS_PER_SECOND + (uint64_t)ts.nanoseconds;
}

static bool from_ns(uint64_t ns, warden_timespec *out) {
    uint64_t secs = ns / NS_PER_SECOND;
    if (secs > INT64_MAX) return false;
    out->seconds = (int64_t)secs;
    out->nanoseconds = (int32_t)(ns % NS_PER_SECOND);
    return true;
}

void warden_time_init(warden_time_service *service,
                      warden_time_source source, void *context) {
    if (!service) return;
    service->source = source;
    service->source_context = context;
    service->monotonic_last_ns = 0;
    service->virtual_ns = 0;
    service->monotonic_seen = false;
    service->realtime_trusted = false;
    service->realtime_uncertainty_ns = 0;
}

void warden_time_set_realtime_authority(warden_time_service *service,
                                       bool trusted, uint64_t uncertainty_ns) {
    if (!service) return;
    service->realtime_trusted = trusted;
    service->realtime_uncertainty_ns = trusted ? uncertainty_ns : 0;
}

warden_time_status warden_time_advance_virtual(warden_time_service *service,
                                               uint64_t delta_ns) {
    if (!service) return WARDEN_TIME_INVALID;
    if (delta_ns > UINT64_MAX - service->virtual_ns) return WARDEN_TIME_OVERFLOW;
    service->virtual_ns += delta_ns;
    return WARDEN_TIME_OK;
}

warden_time_status warden_time_read(warden_time_service *service,
    warden_clock_kind kind, bool capability_allowed, warden_timespec *out,
    uint64_t *uncertainty_ns) {
    if (!service || !out) return WARDEN_TIME_INVALID;
    if (!capability_allowed) return WARDEN_TIME_DENIED;
    if (kind != WARDEN_CLOCK_MONOTONIC && kind != WARDEN_CLOCK_VIRTUAL &&
        kind != WARDEN_CLOCK_REALTIME) return WARDEN_TIME_INVALID;
    warden_timespec candidate;
    uint64_t value = 0;
    if (kind == WARDEN_CLOCK_VIRTUAL) {
        value = service->virtual_ns;
        if (!from_ns(value, &candidate)) return WARDEN_TIME_OVERFLOW;
    } else {
        if (kind == WARDEN_CLOCK_REALTIME && !service->realtime_trusted)
            return WARDEN_TIME_UNAVAILABLE;
        if (!service->source || !service->source(service->source_context, kind, &candidate))
            return WARDEN_TIME_UNAVAILABLE;
        if (!valid_spec(candidate)) return WARDEN_TIME_INVALID;
        if ((uint64_t)candidate.seconds >
            (UINT64_MAX - (uint64_t)candidate.nanoseconds) / NS_PER_SECOND)
            return WARDEN_TIME_OVERFLOW;
        value = to_ns(candidate);
        if (kind == WARDEN_CLOCK_MONOTONIC && service->monotonic_seen &&
            value < service->monotonic_last_ns) return WARDEN_TIME_REGRESSION;
    }
    /* Commit only on success; a refusal never changes the caller's output. */
    *out = candidate;
    if (uncertainty_ns) *uncertainty_ns = kind == WARDEN_CLOCK_REALTIME ?
        service->realtime_uncertainty_ns : 0;
    if (kind == WARDEN_CLOCK_MONOTONIC) {
        service->monotonic_last_ns = value;
        service->monotonic_seen = true;
    }
    return WARDEN_TIME_OK;
}
