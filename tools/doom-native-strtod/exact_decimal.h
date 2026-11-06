/* Fixed-budget exact decimal -> binary64 round-to-nearest-even experiment.
 * No malloc, host I/O, guest syscalls, or locale. 512 uint32 limbs per bigint.
 * Returns 0 for unsupported long inputs; caller falls back to its approximate
 * diagnostic path. Not a complete strtod: rounding modes and locale unsupported.
 */
#ifndef WARDEN_DOOM_EXACT_DECIMAL_H
#define WARDEN_DOOM_EXACT_DECIMAL_H
#define WARDEN_BIG_LIMBS 512
#define WARDEN_MAX_DIGITS 3000
struct warden_big {
    uint32_t word[WARDEN_BIG_LIMBS];
    unsigned n;
};
static void wb_set(struct warden_big *b, uint32_t v) {
    b->n = v ? 1U : 0U;
    if (v) b->word[0] = v;
}
static int wb_mul_small(struct warden_big *b, uint32_t x) {
    uint64_t carry = 0;
    for (unsigned i = 0; i < b->n; ++i) {
        uint64_t t = (uint64_t)b->word[i] * x + carry;
        b->word[i] = (uint32_t)t;
        carry = t >> 32;
    }
    if (carry) {
        if (b->n == WARDEN_BIG_LIMBS) return 0;
        b->word[b->n++] = (uint32_t)carry;
    }
    return 1;
}
static int wb_add_digit(struct warden_big *b, uint32_t x) {
    unsigned i = 0;
    uint64_t carry = x;
    while (carry && i < b->n) {
        uint64_t t = (uint64_t)b->word[i] + carry;
        b->word[i++] = (uint32_t)t;
        carry = t >> 32;
    }
    if (carry) {
        if (b->n == WARDEN_BIG_LIMBS) return 0;
        b->word[b->n++] = (uint32_t)carry;
    }
    return 1;
}
static int wb_mul_u64(struct warden_big *out, const struct warden_big *in, uint64_t x) {
    __uint128_t carry = 0;
    if (!x || !in->n) { wb_set(out, 0); return 1; }
    for (unsigned i = 0; i < in->n; ++i) {
        __uint128_t t = (__uint128_t)in->word[i] * x + carry;
        out->word[i] = (uint32_t)t;
        carry = t >> 32;
    }
    unsigned n = in->n;
    while (carry) {
        if (n == WARDEN_BIG_LIMBS) return 0;
        out->word[n++] = (uint32_t)carry;
        carry >>= 32;
    }
    out->n = n;
    return 1;
}
static int wb_shift(struct warden_big *out, const struct warden_big *in, unsigned bits) {
    if (!in->n) { wb_set(out, 0); return 1; }
    unsigned limb = bits / 32U, rem = bits % 32U;
    if (limb >= WARDEN_BIG_LIMBS || in->n > WARDEN_BIG_LIMBS - limb) return 0;
    unsigned n = limb + in->n;
    for (unsigned i = 0; i < limb; ++i) out->word[i] = 0;
    uint32_t carry = 0;
    for (unsigned i = 0; i < in->n; ++i) {
        uint32_t x = in->word[i];
        out->word[limb + i] = rem ? (x << rem) | carry : x;
        carry = rem ? (x >> (32U - rem)) : 0;
    }
    if (carry) {
        if (n == WARDEN_BIG_LIMBS) return 0;
        out->word[n++] = carry;
    }
    out->n = n;
    return 1;
}
static int wb_cmp(const struct warden_big *a, const struct warden_big *b) {
    if (a->n != b->n) return a->n > b->n ? 1 : -1;
    for (unsigned i = a->n; i > 0; --i) {
        if (a->word[i-1] != b->word[i-1]) return a->word[i-1] > b->word[i-1] ? 1 : -1;
    }
    return 0;
}
static void wb_double_parts(uint64_t bits, uint64_t *m, int *exp) {
    unsigned biased = (unsigned)((bits >> 52) & 2047U);
    *m = bits & ((1ULL << 52)-1);
    if (biased) { *m |= (1ULL << 52); *exp = (int)biased - 1075; }
    else *exp = -1074;
}
/* Compare NUM * 2^E / DEN with the midpoint after the finite double
 * 'lo'. hi=INF uses the correct overflow-rounding boundary. */
static int wb_cmp_mid(const struct warden_big *num, const struct warden_big *den,
                      int E, uint64_t lo, int *out) {
    uint64_t m;
    int exp;
    if (lo == 0x7fefffffffffffffULL) {
        m = (1ULL << 54) - 1ULL;
        exp = 970;
    } else {
        uint64_t lm, hm;
        int le, he;
        wb_double_parts(lo, &lm, &le);
        wb_double_parts(lo+1, &hm, &he);
        if (le < he) { m = lm + (hm << (unsigned)(he-le)); exp = le-1; }
        else { m = (lm << (unsigned)(le-he)) + hm; exp = he-1; }
    }
    struct warden_big rhs, shifted;
    if (!wb_mul_u64(&rhs, den, m)) return 0;
    if (E >= exp) {
        if ((long)E - exp > 12000 || !wb_shift(&shifted, num, (unsigned)(E-exp))) return 0;
        *out = wb_cmp(&shifted, &rhs);
    } else {
        if ((long)exp - E > 12000 || !wb_shift(&shifted, &rhs, (unsigned)(exp-E))) return 0;
        *out = wb_cmp(num, &shifted);
    }
    return 1;
}
/* Compare exact positive rational num*2^E/den to finite binary64.
 * Success is 1; 0 means bounded integer capacity prevented comparison.
 * Inexact results are not errors by themselves. */
static int wb_compare_double(const struct warden_big *num,
                             const struct warden_big *den,
                             int E, uint64_t bits, int *cmp) {
    if (bits >= 0x7ff0000000000000ULL) return 0;
    uint64_t m;
    int exp;
    wb_double_parts(bits, &m, &exp);
    struct warden_big rhs, shifted;
    if (!wb_mul_u64(&rhs, den, m)) return 0;
    if (E >= exp) {
        if ((long)E - exp > 12000 || !wb_shift(&shifted, num, (unsigned)(E-exp))) return 0;
        *cmp = wb_cmp(&shifted, &rhs);
    } else {
        if ((long)exp - E > 12000 || !wb_shift(&shifted, &rhs, (unsigned)(exp-E))) return 0;
        *cmp = wb_cmp(num, &shifted);
    }
    return 1;
}

/* Return 1 with a binary64 output if all exact operations fit. Return 0
 * without a value if budgets are exceeded. Mode: nearest, ties-to-even;
 * ignores current fenv intentionally and is not production libc.
 */
static int wb_exact_decimal(const char *number_start, const char *number_end,
                            const char *parsed_end, double *result, int *inexact, int *tiny) {
    struct warden_big digits, numerator, denominator;
    wb_set(&digits, 0);
    unsigned total_digits = 0;
    long fractional = 0;
    int seen_dot = 0, significant = 0;
    for (const char *q = number_start; q < number_end; ++q) {
        if (*q == '.') { seen_dot = 1; continue; }
        int d = decimal((unsigned char)*q);
        if (d < 0) return 0;
        if (seen_dot) ++fractional;
        if (!significant && d == 0) continue;
        significant = 1;
        if (++total_digits > WARDEN_MAX_DIGITS) return 0;
        if (!wb_mul_small(&digits, 10) || !wb_add_digit(&digits, (uint32_t)d)) return 0;
    }
    if (!digits.n) { *result = 0.0; *inexact = 0; *tiny = 0; return 1; }
    long ee = -(long)fractional;
    if (parsed_end != number_end) {
        const char *ep = number_end+1;
        long parsed = parse_exp(&ep);
        if (parsed == LONG_MIN || ep != parsed_end) return 0;
        ee = plus_sat(ee, parsed);
    }
    long magnitude = plus_sat((long)total_digits, ee);
    if (magnitude >= 310) { *result = __builtin_inf(); *inexact = 1; *tiny = 0; return 1; }
    if (magnitude <= -325) { *result = 0.0; *inexact = 1; *tiny = 1; return 1; }
    if (ee > 5000 || ee < -5000) return 0;
    int E = (int)ee;
    numerator = digits;
    wb_set(&denominator, 1);
    if (E > 0) {
        for (int j = 0; j < E; ++j)
            if (!wb_mul_small(&numerator, 5)) return 0;
    } else {
        for (int j = 0; j < -E; ++j)
            if (!wb_mul_small(&denominator, 5)) return 0;
    }
    uint64_t left = 0, right = 0x7ff0000000000000ULL;
    while (left < right) {
        uint64_t mid = left + (right - left)/2;
        int cmp;
        if (!wb_cmp_mid(&numerator, &denominator, E, mid, &cmp)) return 0;
        /* At exact halfway ties choose the representable double with even
         * low bit of the encoding. */
        if (cmp > 0 || (cmp == 0 && (mid & 1ULL))) left = mid+1;
        else right = mid;
    }
    union { uint64_t bits; double d; } final;
    final.bits = left;
    int cmp;
    if (final.bits == 0x7ff0000000000000ULL) { *inexact = 1; *tiny = 0; }
    else {
        if (!wb_compare_double(&numerator, &denominator, E, final.bits, &cmp)) return 0;
        *inexact = cmp != 0;
        if (final.bits < 0x0010000000000000ULL) *tiny = 1;
        else if (final.bits > 0x0010000000000000ULL) *tiny = 0;
        else {
            if (!wb_compare_double(&numerator, &denominator, E,
                                   0x0010000000000000ULL, &cmp)) return 0;
            *tiny = cmp < 0;
        }
    }
    *result = final.d;
    return 1;
}
#endif /* WARDEN_DOOM_EXACT_DECIMAL_H */
