/* Experimental bounded C-locale hex-to-binary64; nearest/even only.
 * Depends on exact_decimal.h for fixed-capacity big integer and midpoint
 * comparisons. No allocation, host functions, or I/O.
 */
#ifndef WARDEN_DOOM_EXACT_HEX_H
#define WARDEN_DOOM_EXACT_HEX_H
static int wb_exact_hex(const char *hex_start, const char *digits_end,
                        const char *parsed_end, double *result, int *inexact, int *tiny) {
    struct warden_big value, den;
    wb_set(&value, 0);
    wb_set(&den, 1);
    unsigned significant = 0;
    long frac = 0;
    int seen_dot = 0, any = 0;
    for (const char *q = hex_start; q < digits_end; ++q) {
        if (*q == '.' && !seen_dot) { seen_dot = 1; continue; }
        int d = hex((unsigned char)*q);
        if (d < 0) return 0;
        any = 1;
        if (seen_dot) frac = plus_sat(frac, 1);
        if (!significant && d == 0) continue;
        if (++significant > WARDEN_MAX_DIGITS) return 0;
        if (!wb_mul_small(&value, 16) || !wb_add_digit(&value, (uint32_t)d)) return 0;
    }
    if (!any) return 0;
    if (!value.n) { *result = 0.0; *inexact = 0; *tiny = 0; return 1; }
    long E = plus_sat(0, -4 * frac);
    if (parsed_end != digits_end) {
        const char *q = digits_end + 1;
        long e = parse_exp(&q);
        if (e == LONG_MIN || q != parsed_end) return 0;
        E = plus_sat(E, e);
    }
    unsigned b = 32U;
    uint32_t top = value.word[value.n - 1U];
    while (b && !(top & (1U << (b - 1U)))) --b;
    long order = plus_sat(E, (long)(value.n - 1U) * 32 + b);
    if (order >= 1026) { *result = __builtin_inf(); *inexact = 1; *tiny = 0; return 1; }
    if (order <= -1075) { *result = 0.0; *inexact = 1; *tiny = 1; return 1; }
    if (E > 15000 || E < -15000) return 0;
    uint64_t left = 0, right = 0x7ff0000000000000ULL;
    while (left < right) {
        uint64_t mid = left + (right - left) / 2;
        int cmp;
        if (!wb_cmp_mid(&value, &den, (int)E, mid, &cmp)) return 0;
        if (cmp > 0 || (cmp == 0 && (mid & 1ULL))) left = mid + 1;
        else right = mid;
    }
    union { uint64_t bits; double d; } final;
    final.bits = left;
    int cmp;
    if (final.bits == 0x7ff0000000000000ULL) { *inexact = 1; *tiny = 0; }
    else {
        if (!wb_compare_double(&value, &den, (int)E, final.bits, &cmp)) return 0;
        *inexact = cmp != 0;
        if (final.bits < 0x0010000000000000ULL) *tiny = 1;
        else if (final.bits > 0x0010000000000000ULL) *tiny = 0;
        else {
            if (!wb_compare_double(&value, &den, (int)E,
                                   0x0010000000000000ULL, &cmp)) return 0;
            *tiny = cmp < 0;
        }
    }
    *result = final.d;
    return 1;
}
#endif
