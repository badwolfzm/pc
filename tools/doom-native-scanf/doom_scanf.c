/* Freestanding Doom-specific integer scanner. Not a general sscanf. */
#include <stdarg.h>
#include <limits.h>
#include <stdint.h>

static int equal(const char *a, const char *b) {
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static int white(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}
static int digit(unsigned char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
/* Overflow is rejected instead of performing implementation-dependent assignment.
   The supported Doom call sites provide int/unsigned int destinations. */
static int parse_int(const char *input, int base, int unsig, int *out) {
    const unsigned char *p = (const unsigned char *)input;
    while (white(*p)) ++p;
    if (!*p) return -1;
    int neg = 0;
    if (*p == '+' || *p == '-') { neg = *p == '-'; ++p; }
    if (base == 0) {
        if (*p == '0') {
            base = 8;
            if ((p[1] == 'x' || p[1] == 'X') && digit(p[2]) >= 0 && digit(p[2]) < 16) {
                base = 16; p += 2;
            }
        } else base = 10;
    } else if (base == 16 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X') && digit(p[2]) >= 0 && digit(p[2]) < 16) {
        p += 2;
    }
    uint64_t number = 0;
    int consumed = 0;
    const uint64_t limit = unsig ? UINT_MAX : (neg ? (uint64_t)INT_MAX + 1U : INT_MAX);
    for (;;) {
        int d = digit(*p);
        if (d < 0 || d >= base) break;
        if (number > (limit - (unsigned)d) / (unsigned)base) return 0;
        number = number * (unsigned)base + (unsigned)d;
        ++p;
        consumed = 1;
    }
    if (!consumed) return 0;
    if (unsig) {
        unsigned value = (unsigned)number;
        if (neg) value = 0U - value;
        *(unsigned *)out = value;
    } else if (neg && number == (uint64_t)INT_MAX + 1U) {
        *out = INT_MIN;
    } else {
        *out = neg ? -(int)number : (int)number;
    }
    return 1;
}

int __isoc99_sscanf(const char *source, const char *fmt, ...) {
    if (!source || !fmt) return -1;
    int base, unsig = 0;
    const char *p = source;
    if (equal(fmt, " 0x%x")) {
        while (white((unsigned char)*p)) ++p;
        if (*p != '0') return *p ? 0 : -1;
        ++p;
        if (*p != 'x') return *p ? 0 : -1;
        ++p;
        base = 16; unsig = 1;
    } else if (equal(fmt, " 0X%x")) {
        while (white((unsigned char)*p)) ++p;
        if (*p != '0') return *p ? 0 : -1;
        ++p;
        if (*p != 'X') return *p ? 0 : -1;
        ++p;
        base = 16; unsig = 1;
    } else if (equal(fmt, " 0%o")) {
        while (white((unsigned char)*p)) ++p;
        if (*p != '0') return *p ? 0 : -1;
        ++p;
        base = 8; unsig = 1;
    } else if (equal(fmt, " %d")) {
        base = 10;
    } else if (equal(fmt, "%x")) {
        base = 16; unsig = 1;
    } else if (equal(fmt, "%i")) {
        base = 0;
    } else {
        return -1;
    }
    va_list args;
    va_start(args, fmt);
    int *out = va_arg(args, int *);
    va_end(args);
    if (!out) return -1;
    int result = parse_int(p, base, unsig, out);
    if (result == 0 && *p == '\0') return -1;
    return result;
}
