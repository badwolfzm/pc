/* Narrow freestanding Doom snprintf/vsnprintf implementation.
 * Strict C-locale integer/string formatting; unsupported formats fail closed.
 * No heap, syscalls, file/process API, floating point, or mutable globals.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>
#include <limits.h>
#include <inttypes.h>

typedef struct {
    char *dst;
    size_t capacity;
    size_t count; /* output length independent of truncation */
    int error;
} out_t;

static void repeat(out_t *o, char c, size_t n) {
    if (o->error) return;
    if (n > (size_t)INT_MAX - o->count) { o->error = 1; return; }
    size_t writable = 0;
    if (o->capacity && o->count < o->capacity - 1) {
        writable = (o->capacity - 1) - o->count;
        if (writable > n) writable = n;
    }
    for (size_t i = 0; i < writable; i++) o->dst[o->count + i] = c;
    o->count += n;
}

static void append(out_t *o, const char *s, size_t n) {
    if (o->error) return;
    if (n > (size_t)INT_MAX - o->count) { o->error = 1; return; }
    size_t writable = 0;
    if (o->capacity && o->count < o->capacity - 1) {
        writable = (o->capacity - 1) - o->count;
        if (writable > n) writable = n;
    }
    for (size_t i = 0; i < writable; i++) o->dst[o->count + i] = s[i];
    o->count += n;
}

static int number(const char **p, int *value) {
    int n = 0;
    while (**p >= '0' && **p <= '9') {
        int digit = *(*p)++ - '0';
        if (n > (INT_MAX - digit) / 10) return 0;
        n = n * 10 + digit;
    }
    *value = n;
    return 1;
}

enum lenmod { LEN_DEFAULT, LEN_HH, LEN_H, LEN_L, LEN_LL, LEN_J, LEN_Z, LEN_T };

static intmax_t signed_value(va_list *ap, enum lenmod len) {
    switch (len) {
        case LEN_HH: return (signed char)va_arg(*ap, int);
        case LEN_H: return (short)va_arg(*ap, int);
        case LEN_L: return va_arg(*ap, long);
        case LEN_LL: return va_arg(*ap, long long);
        case LEN_J: return va_arg(*ap, intmax_t);
        case LEN_Z: return va_arg(*ap, ptrdiff_t); /* LP64 target */
        case LEN_T: return va_arg(*ap, ptrdiff_t);
        default: return va_arg(*ap, int);
    }
}

static uintmax_t unsigned_value(va_list *ap, enum lenmod len) {
    switch (len) {
        case LEN_HH: return (unsigned char)va_arg(*ap, unsigned int);
        case LEN_H: return (unsigned short)va_arg(*ap, unsigned int);
        case LEN_L: return va_arg(*ap, unsigned long);
        case LEN_LL: return va_arg(*ap, unsigned long long);
        case LEN_J: return va_arg(*ap, uintmax_t);
        case LEN_Z: return va_arg(*ap, size_t);
        case LEN_T: return va_arg(*ap, size_t); /* unsigned ptrdiff_t on LP64 */
        default: return va_arg(*ap, unsigned int);
    }
}

static void field_text(out_t *o, const char *src, size_t n, int width, int left) {
    size_t padding = (width > 0 && (size_t)width > n) ? (size_t)width - n : 0;
    if (!left) repeat(o, ' ', padding);
    append(o, src, n);
    if (left) repeat(o, ' ', padding);
}

static void field_number(out_t *o, uintmax_t value, int base, int uppercase,
                         int negative, int signed_conv, int alt,
                         int plus, int space, int left, int zero,
                         int width, int precision, int precision_given) {
    uintmax_t original = value;
    char reverse[sizeof(uintmax_t) * CHAR_BIT + 1];
    size_t ndigits = 0;
    if (value || !precision_given || precision != 0) {
        do {
            unsigned digit = (unsigned)(value % (uintmax_t)base);
            reverse[ndigits++] = (char)(digit < 10 ? '0' + digit
                           : (uppercase ? 'A' : 'a') + digit - 10);
            value /= (uintmax_t)base;
        } while (value);
    }
    /* %#o without precision is a leading zero, but unlike precision it
       does not disable the '0' width flag. With precision use extra digit. */
    if (alt && base == 8 && original == 0 && precision_given && precision == 0) {
        reverse[0] = '0';
        ndigits = 1;
    }
    if (alt && base == 8 && original != 0 && precision_given &&
        precision <= (int)ndigits) precision = (int)ndigits + 1;
    char sign = negative ? '-' : (signed_conv && plus ? '+' : (signed_conv && space ? ' ' : 0));
    char prefix[2];
    size_t prefix_size = 0;
    if (alt && base == 8 && original != 0 && !precision_given) {
        prefix[0] = '0'; prefix_size = 1;
    } else if (alt && base == 16 && original != 0) {
        prefix[0] = '0'; prefix[1] = uppercase ? 'X' : 'x'; prefix_size = 2;
    }
    size_t leading_zeros = precision_given && (size_t)precision > ndigits
                               ? (size_t)precision - ndigits : 0;
    size_t body = (sign ? 1u : 0u) + prefix_size + leading_zeros + ndigits;
    size_t pad = width > 0 && (size_t)width > body ? (size_t)width - body : 0;
    if (!left && !(zero && !precision_given)) repeat(o, ' ', pad);
    if (sign) repeat(o, sign, 1);
    append(o, prefix, prefix_size);
    if (!left && zero && !precision_given) repeat(o, '0', pad);
    repeat(o, '0', leading_zeros);
    while (ndigits) { --ndigits; repeat(o, reverse[ndigits], 1); }
    if (left) repeat(o, ' ', pad);
}

int vsnprintf(char *restrict dst, size_t capacity, const char *restrict fmt, va_list ap) {
    out_t o = {dst, capacity, 0, 0};
    if (!fmt || (capacity != 0 && !dst)) return -1;
    va_list args;
    va_copy(args, ap);
    const char *p = fmt;
    while (*p && !o.error) {
        if (*p != '%') { repeat(&o, *p++, 1); continue; }
        ++p;
        int left = 0, plus = 0, space = 0, alt = 0, zero = 0;
        for (;;) {
            if (*p == '-') left = 1;
            else if (*p == '+') plus = 1;
            else if (*p == ' ') space = 1;
            else if (*p == '#') alt = 1;
            else if (*p == '0') zero = 1;
            else break;
            ++p;
        }
        int width = 0;
        if (*p == '*') {
            int w = va_arg(args, int);
            ++p;
            if (w == INT_MIN) { o.error = 1; break; }
            if (w < 0) { left = 1; width = -w; }
            else width = w;
        } else if (!number(&p, &width)) { o.error = 1; break; }
        int precision = 0, precision_given = 0;
        if (*p == '.') {
            ++p;
            precision_given = 1;
            if (*p == '*') {
                precision = va_arg(args, int);
                ++p;
                if (precision < 0) { precision = 0; precision_given = 0; }
            } else if (!number(&p, &precision)) { o.error = 1; break; }
        }
        enum lenmod len = LEN_DEFAULT;
        if (*p == 'h') { len = LEN_H; ++p; if (*p == 'h') { len = LEN_HH; ++p; } }
        else if (*p == 'l') { len = LEN_L; ++p; if (*p == 'l') { len = LEN_LL; ++p; } }
        else if (*p == 'j') { len = LEN_J; ++p; }
        else if (*p == 'z') { len = LEN_Z; ++p; }
        else if (*p == 't') { len = LEN_T; ++p; }
        char spec = *p;
        if (!spec) { o.error = 1; break; }
        ++p;
        switch (spec) {
            case '%':
                if (len || precision_given || width || plus || space || alt || zero || left) {
                    o.error = 1; break;
                }
                repeat(&o, '%', 1);
                break;
            case 'c': {
                if (len || precision_given || plus || space || alt || zero) {
                    o.error = 1; break;
                }
                char c = (char)va_arg(args, int);
                field_text(&o, &c, 1, width, left);
                break;
            }
            case 's': {
                if (len || plus || space || alt || zero) { o.error = 1; break; }
                const char *s = va_arg(args, const char *);
                if (!s) { o.error = 1; break; }
                size_t n = 0;
                size_t limit = precision_given ? (size_t)precision : (size_t)INT_MAX;
                while (n < limit && s[n]) ++n;
                field_text(&o, s, n, width, left);
                break;
            }
            case 'd': case 'i': case 'u': case 'x': case 'X': case 'o': {
                int is_signed = spec == 'd' || spec == 'i';
                int base = spec == 'o' ? 8 : (spec == 'x' || spec == 'X' ? 16 : 10);
                uintmax_t magnitude;
                int negative = 0;
                if (is_signed) {
                    intmax_t v = signed_value(&args, len);
                    negative = v < 0;
                    magnitude = negative ? (uintmax_t)(-(v + 1)) + 1u : (uintmax_t)v;
                } else magnitude = unsigned_value(&args, len);
                field_number(&o, magnitude, base, spec == 'X', negative, is_signed,
                             alt && !is_signed, plus, space, left, zero,
                             width, precision, precision_given);
                break;
            }
            default: o.error = 1; break; /* no float, %n, $ position, %p, host I/O */
        }
    }
    va_end(args);
    if (capacity) dst[o.error ? 0 : (o.count < capacity ? o.count : capacity - 1)] = '\0';
    return o.error ? -1 : (int)o.count;
}

int snprintf(char *restrict dst, size_t capacity, const char *restrict fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int rc = vsnprintf(dst, capacity, fmt, ap);
    va_end(ap);
    return rc;
}
