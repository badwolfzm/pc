/* Extra freestanding C-locale string primitives; no host libc dependencies. */
#include <stddef.h>

static unsigned char ascii_fold(unsigned char c) {
    return c >= (unsigned char)'A' && c <= (unsigned char)'Z'
        ? (unsigned char)(c + ('a' - 'A')) : c;
}

int strcasecmp(const char *a, const char *b) {
    for (;;) {
        const unsigned char x = ascii_fold((unsigned char)*a++);
        const unsigned char y = ascii_fold((unsigned char)*b++);
        if (x != y || x == 0) return (int)x - (int)y;
    }
}

int strncasecmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        const unsigned char x = ascii_fold((unsigned char)a[i]);
        const unsigned char y = ascii_fold((unsigned char)b[i]);
        if (x != y || x == 0) return (int)x - (int)y;
    }
    return 0;
}

char *strncpy(char *restrict dst, const char *restrict src, size_t n) {
    size_t i = 0;
    for (; i < n && src[i] != '\0'; ++i) dst[i] = src[i];
    for (; i < n; ++i) dst[i] = '\0';
    return dst;
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
