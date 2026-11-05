#include "linux_entropy_provider.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    warden_entropy_broker b;
    warden_entropy_init(&b);
    unsigned char buffer[258];
    memset(buffer, 0x5A, sizeof(buffer));
    assert(!warden_linux_entropy_fill(NULL, NULL, 1));
    assert(!warden_linux_entropy_fill(NULL, buffer, 257));
    assert(warden_linux_entropy_fill(NULL, NULL, 0));
    assert(buffer[0] == 0x5A);
#if defined(__linux__)
    /* No seeded CSPRNG is also valid: provider must fail closed. */
    bool ready = warden_linux_entropy_attach(&b);
    if (ready) {
        assert(warden_entropy_request(&b, 777, true, true, buffer + 1, 256) == WARDEN_ENTROPY_OK);
        assert(buffer[0] == 0x5A && buffer[257] == 0x5A);
        unsigned char first[256];
        memcpy(first, buffer + 1, sizeof(first));
        assert(warden_entropy_request(&b, 777, true, true, buffer + 1, 256) == WARDEN_ENTROPY_OK);
        /* Collision smoke check; not a cryptographic proof. */
        assert(memcmp(first, buffer + 1, sizeof(first)) != 0);
        warden_entropy_degrade(&b);
        assert(warden_entropy_request(&b, 777, true, true, buffer + 1, 1) == WARDEN_ENTROPY_UNAVAILABLE);
    } else {
        assert(warden_entropy_request(&b, 777, true, true, buffer + 1, 1) == WARDEN_ENTROPY_UNAVAILABLE);
    }
#else
    assert(!warden_linux_entropy_attach(&b));
#endif
    puts("Linux entropy host-provider test passed (not Warden/QEMU verification)");
    return 0;
}
