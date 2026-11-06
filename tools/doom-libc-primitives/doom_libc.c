/* Freestanding reference primitives. Inputs must be valid for the requested length.
 * No allocation, system calls, host I/O or dynamic linking.
 */
#include "doom_libc.h"
#include <stdint.h>

void *wd_memcpy(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    for (size_t i = 0; i < n; ++i) d[i] = s[i];
    return dst;
}
void *wd_memmove(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    /* Compare integer address representations to avoid undefined pointer
       ordering between unrelated objects. Used only on flat x86-64 guests. */
    uintptr_t a = (uintptr_t)dst, b = (uintptr_t)src;
    if (a <= b || a - b >= n) {
        for (size_t i = 0; i < n; ++i) d[i] = s[i];
    } else {
        for (size_t i = n; i != 0; --i) d[i-1] = s[i-1];
    }
    return dst;
}
void *wd_memset(void *dst, int value, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    for (size_t i = 0; i < n; ++i) d[i] = (unsigned char)value;
    return dst;
}
int wd_memcmp(const void *lhs, const void *rhs, size_t n) {
    const unsigned char *a = (const unsigned char *)lhs;
    const unsigned char *b = (const unsigned char *)rhs;
    for (size_t i = 0; i < n; ++i) {
        if (a[i] != b[i]) return (int)a[i] - (int)b[i];
    }
    return 0;
}
size_t wd_strlen(const char *s) {
    size_t n = 0;
    while (s[n] != '\0') ++n;
    return n;
}
size_t wd_strnlen(const char *s, size_t bound) {
    size_t n = 0;
    while (n < bound && s[n] != '\0') ++n;
    return n;
}
/* A native linker may opt-in to these exact libc names. This must be compiled
   freestanding with -fno-builtin and verified with the target engine build. */
#ifdef WARDEN_DOOM_EXPORT_LIBC_NAMES
void *memcpy(void *d, const void *s, size_t n) { return wd_memcpy(d, s, n); }
void *memmove(void *d, const void *s, size_t n) { return wd_memmove(d, s, n); }
void *memset(void *d, int c, size_t n) { return wd_memset(d, c, n); }
int memcmp(const void *a, const void *b, size_t n) { return wd_memcmp(a, b, n); }
size_t strlen(const char *s) { return wd_strlen(s); }
size_t strnlen(const char *s, size_t n) { return wd_strnlen(s, n); }
#endif
