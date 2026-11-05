#define _GNU_SOURCE
#include "linux_entropy_provider.h"
#include <errno.h>
#include <string.h>
#if defined(__linux__)
#include <sys/random.h>
#endif
static void wipe(void *p, size_t n) {
    volatile unsigned char *v = (volatile unsigned char *)p;
    while (n--) *v++ = 0;
}
bool warden_linux_entropy_fill(void *context, uint8_t *destination, size_t length) {
    (void)context;
    if (length > WARDEN_ENTROPY_MAX_REQUEST || (length != 0 && destination == NULL)) return false;
#if defined(__linux__)
    /* Stage all bytes: on failure, leave caller memory untouched. */
    unsigned char staging[WARDEN_ENTROPY_MAX_REQUEST];
    size_t position = 0;
    unsigned attempts = 0;
    while (position < length) {
        if (++attempts > 16u) { wipe(staging, sizeof(staging)); return false; }
        /* GRND_NONBLOCK refuses to wait for an uninitialized CSPRNG. */
        ssize_t count = getrandom(staging + position, length - position, GRND_NONBLOCK);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) { wipe(staging, sizeof(staging)); return false; }
        position += (size_t)count;
    }
    if (length) memcpy(destination, staging, length);
    wipe(staging, sizeof(staging));
    return true;
#else
    (void)destination;
    return length == 0; /* Never attach successfully off Linux. */
#endif
}
bool warden_linux_entropy_attach(warden_entropy_broker *broker) {
    if (!broker) return false;
    uint8_t probe[32];
#if defined(__linux__)
    bool ready = warden_linux_entropy_fill(NULL, probe, sizeof(probe));
#else
    bool ready = false;
#endif
    wipe(probe, sizeof(probe));
    if (!ready) {
        warden_entropy_attach(broker, NULL, NULL, false);
        return false;
    }
    warden_entropy_attach(broker, warden_linux_entropy_fill, NULL, true);
    return true;
}
