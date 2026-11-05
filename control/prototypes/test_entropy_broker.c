#include "entropy_broker.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
/* Predictable test provider only: NEVER use as production entropy. */
typedef struct { size_t calls; bool fail; } test_provider;
static bool fill_test(void *ctx, uint8_t *out, size_t n) {
    test_provider *p = ctx;
    p->calls++;
    memset(out, 0xA7, n);
    return !p->fail;
}
static void unchanged(const uint8_t *buf, size_t n) {
    for (size_t i = 0; i < n; ++i) assert(buf[i] == 0x5A);
}
int main(void) {
    warden_entropy_broker b;
    uint8_t out[257];
    test_provider p = {0};
    warden_entropy_init(&b);
    memset(out, 0x5A, sizeof(out));
    assert(warden_entropy_request(&b, 1, true, true, out, 16) == WARDEN_ENTROPY_UNAVAILABLE);
    unchanged(out, sizeof(out));
    warden_entropy_attach(&b, fill_test, &p, true); /* simulated trust ONLY */
    assert(warden_entropy_request(&b, 0, true, true, out, 16) == WARDEN_ENTROPY_DENIED);
    assert(warden_entropy_request(&b, 1, false, true, out, 16) == WARDEN_ENTROPY_DENIED);
    assert(warden_entropy_request(&b, 1, true, false, out, 16) == WARDEN_ENTROPY_INVALID);
    assert(warden_entropy_request(&b, 1, true, true, out, 257) == WARDEN_ENTROPY_INVALID);
    assert(warden_entropy_request(&b, 1, true, true, NULL, 1) == WARDEN_ENTROPY_INVALID);
    assert(p.calls == 0); unchanged(out, sizeof(out));
    assert(warden_entropy_request(&b, 1, true, true, NULL, 0) == WARDEN_ENTROPY_OK);
    assert(p.calls == 0);
    assert(warden_entropy_request(&b, 1, true, true, out, 256) == WARDEN_ENTROPY_OK);
    assert(p.calls == 1 && out[0] == 0xA7 && out[255] == 0xA7 && out[256] == 0x5A);
    for (int i=0;i<3;i++) assert(warden_entropy_request(&b, 1, true, true, out, 256) == WARDEN_ENTROPY_OK);
    assert(warden_entropy_request(&b, 1, true, true, out, 1) == WARDEN_ENTROPY_LIMIT);
    assert(p.calls == 4);
    for (uint64_t id=2; id<=WARDEN_ENTROPY_CLIENTS; ++id)
        assert(warden_entropy_request(&b, id, true, true, out, 1) == WARDEN_ENTROPY_OK);
    assert(warden_entropy_request(&b, 999, true, true, out, 1) == WARDEN_ENTROPY_LIMIT);
    warden_entropy_init(&b);
    warden_entropy_attach(&b, fill_test, &p, true);
    p.fail = true; memset(out, 0x5A, sizeof(out));
    assert(warden_entropy_request(&b, 1, true, true, out, 16) == WARDEN_ENTROPY_PROVIDER_FAILED);
    unchanged(out, sizeof(out));
    assert(warden_entropy_request(&b, 1, true, true, out, 16) == WARDEN_ENTROPY_UNAVAILABLE);
    assert(b.clients[0].calls == 0 && b.clients[0].bytes == 0);
    p.fail = false; warden_entropy_attach(&b, fill_test, &p, true);
    assert(warden_entropy_request(&b, 1, true, true, out, 16) == WARDEN_ENTROPY_OK);
    warden_entropy_degrade(&b);
    assert(warden_entropy_request(&b, 1, true, true, out, 1) == WARDEN_ENTROPY_UNAVAILABLE);
    puts("entropy broker prototype: all assertions passed (not cryptographic verification)");
    return 0;
}
