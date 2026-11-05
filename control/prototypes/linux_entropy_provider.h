#ifndef WARDEN_LINUX_ENTROPY_PROVIDER_H
#define WARDEN_LINUX_ENTROPY_PROVIDER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "entropy_broker.h"
/* Linux-host-only provider. Trusts the host kernel CSPRNG. Not a guest ABI. */
bool warden_linux_entropy_fill(void *context, uint8_t *destination, size_t length);
/* Probes nonblocking getrandom before enabling broker. Fail-closed on error. */
bool warden_linux_entropy_attach(warden_entropy_broker *broker);
#endif
