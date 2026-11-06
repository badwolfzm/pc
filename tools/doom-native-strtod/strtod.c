/* Bounded C-locale decimal/hex conversion prototype; not correctly rounded.
 * Proof-of-link only. NEVER integrate into a production libc without a
 * standards-conformance and independent math review.
 *
 * No host I/O, allocation, locale, or process calls. Thread-local errno is
 * deliberately left as an unresolved requirement for the guest runtime.
 */
#include <errno.h>
#include <float.h>
#include <limits.h>
#include <stdint.h>

static int space(unsigned char c) {
    return c == ' ' || c == '\n' || c == '\t' || c == '\r' || c == '\v' || c == '\f';
}
static int decimal(unsigned char c) {
    return c >= '0' && c <= '9' ? (int)(c - '0') : -1;
}
static int hex(unsigned char c) {
    if (c >= '0' && c <= '9') return (int)(c - '0');
    if (c >= 'a' && c <= 'f') return (int)(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return (int)(c - 'A' + 10);
    return -1;
}
static int eqcase(unsigned char a, unsigned char b) {
    if (a >= 'A' && a <= 'Z') a = (unsigned char)(a - 'A' + 'a');
    return a == b;
}
static int hasprefix(const char *str, const char *prefix) {
    while (*prefix) {
        if (!*str || !eqcase((unsigned char)*str, (unsigned char)*prefix)) return 0;
        ++str; ++prefix;
    }
    return 1;
}
static long parse_exp(const char **p) {
    const char *q = *p;
    int neg = 0;
    if (*q == '+' || *q == '-') { neg = *q == '-'; ++q; }
    if (decimal((unsigned char)*q) < 0) return LONG_MIN;
    long e = 0;
    while (decimal((unsigned char)*q) >= 0) {
        int digit = decimal((unsigned char)*q++);
        if (e < 20000) e = e * 10 + digit;
    }
    *p = q;
    return neg ? -e : e;
}
static long plus_sat(long a, long b) {
    if (b > 0 && a > LONG_MAX - b) return LONG_MAX;
    if (b < 0 && a < LONG_MIN - b) return LONG_MIN;
    return a + b;
}
/* Computing the long-double intermediate avoids most (not all) double-
 * rounding errors in the usual configuration range. The algorithm does not
 * promise correctly-rounded strtod: it intentionally drops trailing digits.
 */
static long double scale10(long double x, long exponent) {
    static const long double p[] = {
        10.L, 100.L, 10000.L, 100000000.L, 1.0e16L,
        1.0e32L, 1.0e64L, 1.0e128L, 1.0e256L,
        1.0e512L, 1.0e1024L, 1.0e2048L, 1.0e4096L
    };
    if (exponent > 4900) return __builtin_infl();
    if (exponent < -4900) return 0.0L;
    unsigned long magnitude = exponent < 0 ? (unsigned long)(-exponent) : (unsigned long)exponent;
    for (unsigned i = 0; magnitude != 0; ++i, magnitude >>= 1) {
        if (magnitude & 1UL) {
            if (exponent < 0) x /= p[i];
            else x *= p[i];
        }
    }
    return x;
}
static long double scale2(long double x, long exponent) {
    static const long double p[] = {
        0x1p1L, 0x1p2L, 0x1p4L, 0x1p8L, 0x1p16L,
        0x1p32L, 0x1p64L, 0x1p128L, 0x1p256L, 0x1p512L,
        0x1p1024L, 0x1p2048L, 0x1p4096L, 0x1p8192L
    };
    if (exponent >= 16384) return __builtin_infl();
    if (exponent <= -16384) return 0.0L;
    unsigned long magnitude = exponent < 0 ? (unsigned long)(-exponent) : (unsigned long)exponent;
    for (unsigned i = 0; magnitude != 0; ++i, magnitude >>= 1) {
        if (magnitude & 1UL) {
            if (exponent < 0) x /= p[i];
            else x *= p[i];
        }
    }
    return x;
}

double strtod(const char *restrict nptr, char **restrict endptr) {
    const char *p = nptr;
    while (space((unsigned char)*p)) ++p;
    int negative = 0;
    if (*p == '-' || *p == '+') { negative = *p == '-'; ++p; }
    if (hasprefix(p, "infinity")) {
        p += 8;
        if (endptr) *endptr = (char *)p;
        return negative ? -__builtin_inf() : __builtin_inf();
    }
    if (hasprefix(p, "inf")) {
        p += 3;
        if (endptr) *endptr = (char *)p;
        return negative ? -__builtin_inf() : __builtin_inf();
    }
    if (hasprefix(p, "nan")) {
        p += 3;
        if (*p == '(') {
            const char *start = p + 1;
            const char *q = start;
            while ((*q >= 'A' && *q <= 'Z') || (*q >= 'a' && *q <= 'z') ||
                   (*q >= '0' && *q <= '9') || *q == '_') ++q;
            if (*q == ')') p = q + 1;
        }
        if (endptr) *endptr = (char *)p;
        double val = __builtin_nan("");
        return negative ? -val : val;
    }
    int is_hex = p[0] == '0' && (p[1] == 'x' || p[1] == 'X') &&
        (hex((unsigned char)p[2]) >= 0 || (p[2] == '.' && hex((unsigned char)p[3]) >= 0));
    if (is_hex) p += 2;
    unsigned long long significand = 0;
    unsigned stored = 0;
    int first_dropped = -1;
    int dot = 0, any = 0;
    long exponent = 0;
    for (;;) {
        unsigned char c = (unsigned char)*p;
        int d = is_hex ? hex(c) : decimal(c);
        if (c == '.' && !dot) { dot = 1; ++p; continue; }
        if (d < 0) break;
        any = 1; ++p;
        if (stored == 0 && d == 0) {
            if (dot) exponent = plus_sat(exponent, is_hex ? -4 : -1);
            continue;
        }
        if (stored < (is_hex ? 16U : 19U)) {
            significand = significand * (is_hex ? 16ULL : 10ULL) + (unsigned)d;
            ++stored;
            if (dot) exponent = plus_sat(exponent, is_hex ? -4 : -1);
        } else {
            if (first_dropped < 0) first_dropped = d;
            if (!dot) exponent = plus_sat(exponent, is_hex ? 4 : 1);
        }
    }
    if (!any) {
        if (endptr) *endptr = (char *)nptr;
        return 0.0;
    }
    if ((!is_hex && (*p == 'e' || *p == 'E')) || (is_hex && (*p == 'p' || *p == 'P'))) {
        const char *q = p + 1;
        long e = parse_exp(&q);
        if (e != LONG_MIN) { exponent = plus_sat(exponent, e); p = q; }
    }
    if (endptr) *endptr = (char *)p;
    if (!significand) return negative ? -0.0 : 0.0;
    if (!is_hex && first_dropped >= 5) ++significand;
    long double ld = (long double)significand;
    ld = is_hex ? scale2(ld, exponent) : scale10(ld, exponent);
    /* An out-of-range result may originate in the long-double intermediate. */
    if (ld > (long double)DBL_MAX) { errno = ERANGE; return negative ? -__builtin_inf() : __builtin_inf(); }
    double d = (double)ld;
    if (d == 0.0) errno = ERANGE;
    return negative ? -d : d;
}
