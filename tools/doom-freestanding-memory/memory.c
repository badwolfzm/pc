#include "memory.h"
#include <stdint.h>

/* Byte-wise, freestanding and deterministic. All nonzero spans must be valid.
 * Prefixed names avoid implicit compiler/libc substitution and recursion.
 */
void *doom_memcpy(void *restrict dst, const void *restrict src, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    for (size_t i = 0; i < n; ++i) d[i] = s[i];
    return dst;
}
void *doom_memset(void *dst, int value, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    for (size_t i = 0; i < n; ++i) d[i] = (unsigned char)value;
    return dst;
}
int doom_memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *x = (const unsigned char *)a;
    const unsigned char *y = (const unsigned char *)b;
    for (size_t i = 0; i < n; ++i) {
        if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
    }
    return 0;
}
void *doom_memmove(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    if (!n || d == s) return dst;
    /* Avoid pointer ordering comparisons between unrelated objects. Use integer
     * distance and only copy backwards when the destination is inside source.
     */
    uintptr_t du = (uintptr_t)dst, su = (uintptr_t)src;
    if (du > su && du - su < n) {
        for (size_t i = n; i != 0; --i) d[i - 1] = s[i - 1];
    } else {
        for (size_t i = 0; i < n; ++i) d[i] = s[i];
    }
    return dst;
}
