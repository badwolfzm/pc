#include "entropy_broker.h"
#include <string.h>
/* Best-effort staging wipe; use platform secure zeroization before release. */
static void wipe(uint8_t *p, size_t n) {
    volatile uint8_t *q = p;
    while (n--) *q++ = 0;
}
void warden_entropy_init(warden_entropy_broker *b) {
    if (b) memset(b, 0, sizeof(*b));
}
void warden_entropy_attach(warden_entropy_broker *b, warden_entropy_fill f,
                           void *ctx, bool ready) {
    if (!b) return;
    b->fill = f; b->provider_context = ctx;
    b->trusted_ready = ready && f != NULL;
}
void warden_entropy_degrade(warden_entropy_broker *b) {
    if (b) b->trusted_ready = false;
}
warden_entropy_status warden_entropy_request(warden_entropy_broker *b,
    uint64_t badge, bool allowed, bool validated, uint8_t *out, size_t n) {
    if (!b) return WARDEN_ENTROPY_INVALID;
    if (!allowed || badge == 0) return WARDEN_ENTROPY_DENIED;
    if (n > WARDEN_ENTROPY_MAX_REQUEST || (n && (!validated || !out)))
        return WARDEN_ENTROPY_INVALID;
    if (!b->trusted_ready || !b->fill) return WARDEN_ENTROPY_UNAVAILABLE;
    if (n == 0) return WARDEN_ENTROPY_OK;
    warden_entropy_client *slot = NULL, *unused = NULL;
    for (size_t i = 0; i < WARDEN_ENTROPY_CLIENTS; ++i) {
        if (b->clients[i].used && b->clients[i].badge == badge) {
            slot = &b->clients[i]; break;
        }
        if (!b->clients[i].used && !unused) unused = &b->clients[i];
    }
    if (!slot) slot = unused;
    if (!slot) return WARDEN_ENTROPY_LIMIT;
    /* Subtraction-based quota comparison cannot overflow. */
    if (slot->calls >= WARDEN_ENTROPY_MAX_CALLS_PER_CLIENT ||
        n > WARDEN_ENTROPY_MAX_BYTES_PER_CLIENT - slot->bytes)
        return WARDEN_ENTROPY_LIMIT;
    /* Provider failure cannot partially overwrite guest-visible output. */
    uint8_t staging[WARDEN_ENTROPY_MAX_REQUEST] = {0};
    if (!b->fill(b->provider_context, staging, n)) {
        wipe(staging, sizeof(staging));
        b->trusted_ready = false;
        return WARDEN_ENTROPY_PROVIDER_FAILED;
    }
    memcpy(out, staging, n);
    wipe(staging, sizeof(staging));
    if (!slot->used) {
        slot->used = true; slot->badge = badge;
        slot->bytes = 0; slot->calls = 0;
    }
    slot->bytes += (uint32_t)n;
    slot->calls++;
    return WARDEN_ENTROPY_OK;
}
