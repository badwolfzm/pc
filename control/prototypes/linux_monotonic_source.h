#ifndef WARDEN_LINUX_MONOTONIC_SOURCE_H
#define WARDEN_LINUX_MONOTONIC_SOURCE_H
#include "time_service.h"
/* Linux host CLOCK_MONOTONIC only. Never supplies UTC or authenticates a guest. */
bool warden_linux_monotonic_source(void *unused, warden_clock_kind kind,
                                   warden_timespec *out);
#endif
