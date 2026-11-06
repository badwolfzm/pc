#include "doom_libc.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static uint32_t state = 0x93e7ac51U;
static uint32_t rand32(void) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}
static void fill(unsigned char *p, size_t n) {
    for (size_t i = 0; i < n; ++i) p[i] = (unsigned char)rand32();
}
int main(void) {
    unsigned char a[512], b[512], c[512], d[512];
    for (unsigned int trial = 0; trial < 10000; ++trial) {
        fill(a, sizeof(a));
        memcpy(b, a, sizeof(a));
        size_t from = rand32() % 512, to = rand32() % 512;
        size_t max_from = 512 - from, max_to = 512 - to;
        size_t n = rand32() % (max_from < max_to ? max_from + 1 : max_to + 1);
        assert(wd_memmove(a + to, a + from, n) == a + to);
        memmove(b + to, b + from, n);
        assert(memcmp(a, b, sizeof(a)) == 0);
        size_t off = rand32() % 512;
        size_t count = rand32() % (513 - off);
        int v = (int)rand32();
        assert(wd_memset(a + off, v, count) == a + off);
        memset(b + off, v, count);
        assert(memcmp(a, b, sizeof(a)) == 0);
        fill(c, sizeof(c));
        fill(d, sizeof(d));
        assert(wd_memcpy(a, c, 512) == a);
        assert(memcmp(a, c, 512) == 0);
        int actual = wd_memcmp(c, d, 512);
        int expected = memcmp(c, d, 512);
        assert((actual > 0) == (expected > 0));
        assert((actual < 0) == (expected < 0));
        assert(wd_memcmp(c, c, 512) == 0);
    }
    char s[] = "warden doom";
    assert(wd_strlen(s) == strlen(s));
    assert(wd_strnlen(s, 0) == 0);
    assert(wd_strnlen(s, 4) == 4);
    assert(wd_strnlen(s, 100) == strlen(s));
    assert(wd_memmove(NULL, NULL, 0) == NULL);
    assert(wd_memcpy(NULL, NULL, 0) == NULL);
    assert(wd_memset(NULL, 0, 0) == NULL);
    assert(wd_memcmp(NULL, NULL, 0) == 0);
    puts("PASS: 10000 differential memory trials and string boundary checks");
    return 0;
}
