#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern int warden_snprintf(char *, size_t, const char *, ...);
#define DIFF(format, ...) do { \
    char actual[200], expected[200]; \
    for (size_t cap = 0; cap <= sizeof(actual); cap += cap < 8 ? 1 : 19) { \
        memset(actual, 'X', sizeof actual); memset(expected, 'X', sizeof expected); \
        int a = warden_snprintf(actual, cap, format, __VA_ARGS__); \
        int e = snprintf(expected, cap, format, __VA_ARGS__); \
        if (a != e || memcmp(actual, expected, sizeof actual) != 0) { \
            fprintf(stderr, "DIFF %s, cap=%zu rc=%d/%d val='%s' vs '%s'\n", \
                    format, cap, a, e, actual, expected); abort(); \
        } \
    } \
} while (0)

int main(void) {
    DIFF("%d", 0); DIFF("%d", -7); DIFF("%i", INT_MIN); DIFF("%u", UINT_MAX);
    DIFF("%ld", LONG_MIN); DIFF("%ld", LONG_MAX);
    DIFF("%lld", LLONG_MIN); DIFF("%llu", ULLONG_MAX);
    DIFF("%zu", (size_t)123); DIFF("%zd", (ptrdiff_t)-123);
    DIFF("%td", (ptrdiff_t)-55);
    DIFF("%08d", -42); DIFF("%-10i", 12); DIFF("%+d", 0);
    DIFF("% d", 42); DIFF("%+10d", 42); DIFF("%010d", -3);
    DIFF("%.0d", 0); DIFF("%+.0d", 0); DIFF("%8.0d", 0);
    DIFF("%.4d", 7); DIFF("%*.5d", 11, 3); DIFF("%*.*d", -10, 3, 21);
    DIFF("%*.*u", 10, -1, 21u); DIFF("%#x", 0u); DIFF("%#08x", 0xfaceu);
    DIFF("%#X", 0xFFFFu); DIFF("%#o", 0u); DIFF("%#.0o", 0u);
    DIFF("%#.4o", 015u); DIFF("%#08o", 012u); DIFF("%#10.6o", 012u);
    DIFF("%08x", 0); DIFF("%hhu", (unsigned int)258); DIFF("%hd", 123);
    DIFF("%s", "hello world"); DIFF("%-10.2s", "hello");
    DIFF("%*.*s", 9, 3, "abcdef"); DIFF("%c", 'X'); DIFF("%5c", 'Z');
    DIFF("%c", 0); DIFF("a%d %s %c!", -42, "x", 'Y');
    char buf[10];
    assert(warden_snprintf(buf, 10, "%f", 1.2) == -1 && buf[0] == 0);
    assert(warden_snprintf(buf, 10, "%n", (int *)0) == -1 && buf[0] == 0);
    assert(warden_snprintf(buf, 10, "%p", (void *)0) == -1 && buf[0] == 0);
    assert(warden_snprintf(buf, 10, "%10000000000d", 3) == -1 && buf[0] == 0);
    assert(warden_snprintf(buf, 10, "%*d", INT_MIN, 3) == -1 && buf[0] == 0);
    assert(warden_snprintf(buf, 10, "%d %s", 3, (const char *)0) == -1 && buf[0] == 0);
    assert(warden_snprintf(NULL, 0, "%s", "abc") == 3);
    assert(warden_snprintf(NULL, 0, "%f", 1.0) == -1);
    puts("PASS Doom strict freestanding snprintf C99 integer/string subset");
    return 0;
}
