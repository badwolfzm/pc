/* mx_input.c -- see include/mx_input.h for the model. */
#include "mx_input.h"
#include <string.h>

const char *mx_status_str(mx_status s) {
    switch (s) {
    case MX_OK: return "ok";
    case MX_E_BUDGET: return "budget";
    case MX_E_RANGE: return "range";
    case MX_E_ENCODING: return "encoding";
    case MX_E_CONTROL: return "control";
    case MX_E_SYNTAX: return "syntax";
    case MX_E_ARG: return "arg";
    }
    return "?";
}

mx_status mx_widen_bytes(mx_untrusted_bytes in, size_t max, mx_frame *out) {
    if (!out || !out->cells || (!in.p && in.n)) return MX_E_ARG;
    if (in.n > max) return MX_E_BUDGET;                 /* ceiling set by the receiver */
    if (in.n >= out->cap) return MX_E_BUDGET;           /* n + 1 must fit, no overflow: n < cap */
    for (size_t i = 0; i < in.n; i++) out->cells[i] = mx_sym_from_cell(mx_cell_from_byte(in.p[i]));
    out->cells[in.n] = mx_sym_from_ctl(mx_ctl_end());
    out->len = (uint32_t)in.n + 1u;
    return MX_OK;
}

/* Decode one UTF-8 scalar at p[0..n). Returns bytes consumed or 0 on error. */
static size_t utf8_decode(const uint8_t *p, size_t n, uint32_t *out) {
    if (n == 0) return 0;
    uint8_t b0 = p[0];
    if (b0 < 0x80) { *out = b0; return 1; }
    size_t need; uint32_t cp, min;
    if (b0 >= 0xC2 && b0 <= 0xDF) { need = 1; cp = b0 & 0x1Fu; min = 0x80; }
    else if (b0 >= 0xE0 && b0 <= 0xEF) { need = 2; cp = b0 & 0x0Fu; min = 0x800; }
    else if (b0 >= 0xF0 && b0 <= 0xF4) { need = 3; cp = b0 & 0x07u; min = 0x10000; }
    else return 0;                                      /* 0x80..0xC1 (continuation/overlong), 0xF5.. */
    if (n < need + 1) return 0;                         /* truncated */
    for (size_t i = 1; i <= need; i++) {
        if ((p[i] & 0xC0u) != 0x80u) return 0;
        cp = (cp << 6) | (p[i] & 0x3Fu);
    }
    if (cp < min) return 0;                             /* overlong */
    if (cp > MX_INPUT_MAX) return 0;
    if (cp >= 0xD800u && cp <= 0xDFFFu) return 0;       /* surrogate */
    *out = cp;
    return need + 1;
}

mx_status mx_widen_utf8(mx_untrusted_bytes in, size_t max, mx_frame *out, size_t *consumed) {
    if (consumed) *consumed = 0;
    if (!out || !out->cells || (!in.p && in.n)) return MX_E_ARG;
    if (in.n > max) return MX_E_BUDGET;
    /* First pass: validate and count, so nothing is written on failure. */
    size_t i = 0, count = 0;
    while (i < in.n) {
        uint32_t cp;
        size_t k = utf8_decode(in.p + i, in.n - i, &cp);
        if (k == 0) { if (consumed) *consumed = i; return MX_E_ENCODING; }
        i += k; count++;
    }
    if (count >= out->cap) { if (consumed) *consumed = i; return MX_E_BUDGET; }
    /* Second pass: write. */
    i = 0; count = 0;
    while (i < in.n) {
        uint32_t cp; mx_cell c;
        size_t k = utf8_decode(in.p + i, in.n - i, &cp);
        if (k == 0 || mx_cell_from_scalar(cp, &c) != MX_OK) return MX_E_ENCODING; /* unreachable after pass 1 */
        out->cells[count++] = mx_sym_from_cell(c);
        i += k;
    }
    out->cells[count] = mx_sym_from_ctl(mx_ctl_end());
    out->len = (uint32_t)count + 1u;
    if (consumed) *consumed = i;
    return MX_OK;
}

uint32_t mx_frame_input_len(const mx_frame *f) {
    if (!f || !f->cells) return 0;
    uint32_t n = f->len < f->cap ? f->len : f->cap;
    for (uint32_t i = 0; i < n; i++) if (!mx_sym_is_input(f->cells[i])) return i;
    return n;
}

static mx_status cell_at(const mx_frame *f, uint32_t i, mx_cell *out) {
    return mx_sym_cell(f->cells[i], out);
}

mx_status mx_validate_u32_dec(const mx_frame *f, uint32_t lo, uint32_t hi, uint32_t *out) {
    if (!f || !f->cells || !out) return MX_E_ARG;
    uint32_t n = mx_frame_input_len(f);
    if (n == 0 || n > 10) return MX_E_SYNTAX;           /* uint32 has at most 10 digits */
    uint64_t acc = 0;
    for (uint32_t i = 0; i < n; i++) {
        mx_cell c; if (cell_at(f, i, &c) != MX_OK) return MX_E_CONTROL;
        if (!mx_cell_in(c, '0', '9')) return MX_E_SYNTAX;
        acc = acc * 10u + (c.v - (uint32_t)'0');        /* <= 9999999999, fits u64 */
    }
    if (acc < lo || acc > hi || acc > 0xFFFFFFFFull) return MX_E_RANGE;
    *out = (uint32_t)acc;
    return MX_OK;
}

mx_status mx_validate_token(const mx_frame *f, const char *allowed, char *out, size_t cap) {
    if (!f || !f->cells || !allowed || !out || cap == 0) return MX_E_ARG;
    uint32_t n = mx_frame_input_len(f);
    if (n == 0) return MX_E_SYNTAX;
    if ((size_t)n >= cap) return MX_E_BUDGET;
    for (uint32_t i = 0; i < n; i++) {
        mx_cell c; if (cell_at(f, i, &c) != MX_OK) return MX_E_CONTROL;
        if (c.v == 0 || c.v > 0x7Fu) return MX_E_SYNTAX;
        if (!strchr(allowed, (int)c.v)) return MX_E_SYNTAX;   /* c.v != 0, so strchr never matches the terminator */
        out[i] = (char)c.v;
    }
    out[n] = 0;
    return MX_OK;
}

mx_status mx_validate_path_segment(const mx_frame *f, char *out, size_t cap) {
    static const char allowed[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-";
    if (!f || !out) return MX_E_ARG;
    uint32_t n = mx_frame_input_len(f);
    if (n > 255) return MX_E_RANGE;
    mx_status s = mx_validate_token(f, allowed, out, cap);
    if (s != MX_OK) return s;
    if (strcmp(out, ".") == 0 || strcmp(out, "..") == 0) { out[0] = 0; return MX_E_SYNTAX; }
    return MX_OK;
}

mx_status mx_validate_enum(const mx_frame *f, const char *const *names, size_t nnames, size_t *index) {
    if (!f || !f->cells || !names || !index) return MX_E_ARG;
    uint32_t n = mx_frame_input_len(f);
    for (size_t k = 0; k < nnames; k++) {
        const char *name = names[k];
        size_t len = strlen(name);
        if (len != n) continue;
        bool match = true;
        for (uint32_t i = 0; i < n && match; i++) {
            mx_cell c; if (cell_at(f, i, &c) != MX_OK) return MX_E_CONTROL;
            match = mx_cell_is(c, name[i]);
        }
        if (match) { *index = k; return MX_OK; }
    }
    return MX_E_SYNTAX;
}

mx_status mx_validate_text(const mx_frame *f, char *out, size_t cap, size_t *outlen) {
    if (outlen) *outlen = 0;
    if (!f || !f->cells || !out || cap == 0) return MX_E_ARG;
    uint32_t n = mx_frame_input_len(f);
    size_t w = 0;
    for (uint32_t i = 0; i < n; i++) {
        mx_cell c; if (cell_at(f, i, &c) != MX_OK) return MX_E_CONTROL;
        uint32_t cp = c.v;
        if (cp < 0x20u && cp != '\t' && cp != '\n') return MX_E_SYNTAX;
        if (cp == 0x7Fu) return MX_E_SYNTAX;
        size_t need = cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;
        if (w + need >= cap) return MX_E_BUDGET;        /* keep room for NUL */
        if (need == 1) out[w++] = (char)cp;
        else if (need == 2) { out[w++] = (char)(0xC0 | (cp >> 6)); out[w++] = (char)(0x80 | (cp & 0x3F)); }
        else if (need == 3) { out[w++] = (char)(0xE0 | (cp >> 12)); out[w++] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[w++] = (char)(0x80 | (cp & 0x3F)); }
        else { out[w++] = (char)(0xF0 | (cp >> 18)); out[w++] = (char)(0x80 | ((cp >> 12) & 0x3F)); out[w++] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[w++] = (char)(0x80 | (cp & 0x3F)); }
    }
    out[w] = 0;
    if (outlen) *outlen = w;
    return MX_OK;
}
