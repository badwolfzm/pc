#include "memory.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t seed = UINT32_C(0x7d215fd3);
static unsigned rand_byte(void) { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed & 255U; }

int main(void) {
    unsigned char a[256], b[256], c[256];
    assert(doom_memcpy(NULL, NULL, 0) == NULL);
    assert(doom_memset(NULL, 123, 0) == NULL);
    assert(doom_memmove(NULL, NULL, 0) == NULL);
    assert(doom_memcmp(NULL, NULL, 0) == 0);
    for (size_t trial = 0; trial < 1000; ++trial) {
        for (size_t i = 0; i < sizeof a; ++i) a[i] = (unsigned char)rand_byte();
        memcpy(b, a, sizeof a);
        memcpy(c, a, sizeof a);
        size_t src = trial % 128U, dst = (trial * 73U) % 128U;
        size_t n = trial % 129U;
        assert(doom_memmove(b + dst, b + src, n) == b + dst);
        memmove(c + dst, c + src, n);
        assert(memcmp(b, c, sizeof b) == 0);
        assert(doom_memcmp(b, c, sizeof b) == 0);
        assert(doom_memset(b + 3, 0x1ab, 250) == b + 3);
        memset(c + 3, 0xab, 250);
        assert(memcmp(b, c, sizeof b) == 0);
        assert(doom_memcpy(b + 3, a + 3, 250) == b + 3);
        memcpy(c + 3, a + 3, 250);
        assert(memcmp(b, c, sizeof b) == 0);
        c[10] = (unsigned char)(b[10] + 1U);
        if (c[10] > b[10]) assert(doom_memcmp(b, c, sizeof b) < 0);
        else assert(doom_memcmp(b, c, sizeof b) > 0);
        assert(doom_memcmp(b, b, sizeof b) == 0);
    }
    puts("PASS: freestanding byte memory primitives, 1000 deterministic boundary/overlap trials");
    return 0;
}
