/* x86-64 Warden Doom: only pure, allocation-free freestanding routines.
 * No I/O, host service, process execution, dynamic allocation, locale state,
 * capabilities or kernel API. ASCII-only case folding is intentional.
 */
#include <stddef.h>
#include <stdint.h>
#include <limits.h>
#include <float.h>
_Static_assert(CHAR_BIT == 8 && sizeof(double) == 8 && DBL_MANT_DIG == 53,
               "Warden x86-64 IEEE-754 double required");

static unsigned char ascii_lower(unsigned char c) {
    if (c >= (unsigned char)'A' && c <= (unsigned char)'Z')
        return (unsigned char)(c + ('a' - 'A'));
    return c;
}

/* ISO C abs is undefined when the positive magnitude is not representable
 * (e.g. INT_MIN). Callers must not supply that value.
 */
int abs(int x) { return x < 0 ? -x : x; }

/* Remove IEEE-754 sign bit, including negative zero and signed NaNs. */
double fabs(double x) {
    union { double d; uint64_t u; } bits = { .d = x };
    bits.u &= UINT64_C(0x7fffffffffffffff);
    return bits.d;
}

/* POSIX names, intentionally ASCII-only and locale-independent. */
int strcasecmp(const char *a, const char *b) {
    for (;;) {
        const unsigned char x = ascii_lower((unsigned char)*a++);
        const unsigned char y = ascii_lower((unsigned char)*b++);
        if (x != y || x == 0) return (int)x - (int)y;
    }
}
int strncasecmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        const unsigned char x = ascii_lower((unsigned char)a[i]);
        const unsigned char y = ascii_lower((unsigned char)b[i]);
        if (x != y || x == 0) return (int)x - (int)y;
    }
    return 0;
}
char *strstr(const char *haystack, const char *needle) {
    if (*needle == '\0') return (char *)haystack;
    for (const char *h = haystack; *h != '\0'; ++h) {
        const char *a = h, *b = needle;
        while (*a != '\0' && *b != '\0' && *a == *b) { ++a; ++b; }
        if (*b == '\0') return (char *)h;
    }
    return NULL;
}
char *strncpy(char *restrict dst, const char *restrict src, size_t n) {
    size_t i = 0;
    for (; i < n && src[i] != '\0'; ++i) dst[i] = src[i];
    for (; i < n; ++i) dst[i] = '\0';
    return dst;
}
