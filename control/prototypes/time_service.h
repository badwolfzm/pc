#ifndef WARDEN_TIME_SERVICE_H
#define WARDEN_TIME_SERVICE_H
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    WARDEN_CLOCK_MONOTONIC = 1,
    WARDEN_CLOCK_VIRTUAL = 2,
    WARDEN_CLOCK_REALTIME = 3
} warden_clock_kind;

typedef enum {
    WARDEN_TIME_OK = 0,
    WARDEN_TIME_DENIED,
    WARDEN_TIME_UNAVAILABLE,
    WARDEN_TIME_INVALID,
    WARDEN_TIME_OVERFLOW,
    WARDEN_TIME_REGRESSION
} warden_time_status;

typedef struct {
    int64_t seconds;
    int32_t nanoseconds;
} warden_timespec;

typedef bool (*warden_time_source)(void *context, warden_clock_kind kind,
                                   warden_timespec *out);

typedef struct {
    warden_time_source source;
    void *source_context;
    uint64_t monotonic_last_ns;
    uint64_t virtual_ns;
    bool monotonic_seen;
    bool realtime_trusted;
    uint64_t realtime_uncertainty_ns;
} warden_time_service;

void warden_time_init(warden_time_service *service, warden_time_source source,
                      void *context);
/* Only a trusted host integration layer may call this after verifying an epoch
   authority's freshness and uncertainty. A guest must never control it. */
void warden_time_set_realtime_authority(warden_time_service *service,
                                        bool trusted, uint64_t uncertainty_ns);
warden_time_status warden_time_advance_virtual(warden_time_service *service,
                                                uint64_t delta_ns);
warden_time_status warden_time_read(warden_time_service *service,
    warden_clock_kind kind, bool capability_allowed, warden_timespec *out,
    uint64_t *uncertainty_ns);

#endif
