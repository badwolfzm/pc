/*
 * mx_input.h -- typed input for MatrixOS / Warden C code.
 *
 * This is the C port of the kernel's Rust cell model
 * (kernel/crates/warden-caps/src/cells.rs). The two must agree; the shared
 * vector file tests/vectors/cells.vec is checked against both.
 *
 * The rule (design doc, "Separate code spaces"):
 *   - Every input byte or Unicode scalar is widened to a 32-bit cell in
 *     0..=0x10FFFF (MX_INPUT_MAX).
 *   - System control values (end of input, framing, commands) live only
 *     above MX_INPUT_MAX. No byte sequence can decode to a control value, so
 *     input can never forge a terminator or a command.
 *   - Untrusted values cannot be used where trusted data is expected; the
 *     only way out is a validator.
 *
 * How C enforces it (no new language, no plugin):
 *   - mx_cell and mx_ctl are distinct one-member structs. C has no implicit
 *     conversion for structs, so a cell cannot be an array index, a shift
 *     count, a size, an enum, a pointer, or compared to a bare constant.
 *     Mixing them is a hard compile error on every C compiler.
 *   - mx_untrusted_* wrappers do the same for raw bytes and integers that
 *     arrive from outside the trust boundary.
 *   - Validators (mx_validate_*) are the only functions that write a plain C
 *     value from input, and each one checks a receiver-chosen bound first.
 *   - Buffers are receiver-sized: every widen/validate takes the receiver's
 *     capacity and fails with MX_E_BUDGET instead of growing or truncating.
 *   - mxcc --typed-input includes this header into every translation unit
 *     and promotes the conversion warnings that would otherwise let an
 *     integer sneak through a prototype-less call.
 *
 * Status vocabulary used in the README: behaviour below is unit-tested and
 * fuzzed under ASan/UBSan (see tests/unit/test_input.c). It has not been
 * exercised inside the kernel yet.
 */
#ifndef MX_INPUT_H
#define MX_INPUT_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- constants (must equal cells.rs) ------------------------------- */
#define MX_INPUT_MAX   0x10FFFFu   /* highest value an input cell may hold   */
#define MX_CTRL_END    0x110000u   /* end of input                           */
#define MX_CTRL_FRAME  0x110001u   /* frame boundary                         */
#define MX_CTRL_FIRST  MX_CTRL_END /* every control value is >= this         */

/* ---- status --------------------------------------------------------- */
typedef enum {
    MX_OK = 0,
    MX_E_BUDGET   = 1,   /* receiver ceiling exceeded (never grow, never truncate) */
    MX_E_RANGE    = 2,   /* value outside the allowed range                        */
    MX_E_ENCODING = 3,   /* malformed UTF-8 / not a scalar value                   */
    MX_E_CONTROL  = 4,   /* control value where input expected, or vice versa      */
    MX_E_SYNTAX   = 5,   /* validator grammar not matched                          */
    MX_E_ARG      = 6    /* NULL or inconsistent arguments                          */
} mx_status;

/* ---- the cell types ------------------------------------------------- */

/* An input cell. Invariant: v <= MX_INPUT_MAX. Only the constructors below
 * produce one; code that touches .v directly is outside the model and is
 * what code review looks for (grep for "\.v\b" in non-validator code). */
typedef struct mx_cell { uint32_t v; } mx_cell;

/* A control value. Invariant: v > MX_INPUT_MAX. */
typedef struct mx_ctl { uint32_t v; } mx_ctl;

/* A symbol: one slot of a mixed stream, holding either a cell or a control
 * value. The ranges are disjoint, so mx_sym_is_input is exact. This is the
 * shape the Rust side calls Cell (its Cell covers both ranges; the C split
 * into mx_cell / mx_ctl is what makes misuse a compile error). */
typedef struct mx_sym { uint32_t v; } mx_sym;

/* Rust Cell::from_byte. Always lands in the input range. */
static inline mx_cell mx_cell_from_byte(uint8_t b) { mx_cell c = { (uint32_t)b }; return c; }

/* Rust Cell::from_char. A Unicode scalar is <= 0x10FFFF and not a surrogate.
 * Rust guarantees this by the char type; C must check. */
static inline mx_status mx_cell_from_scalar(uint32_t scalar, mx_cell *out) {
    if (!out) return MX_E_ARG;
    if (scalar > MX_INPUT_MAX) return MX_E_RANGE;
    if (scalar >= 0xD800u && scalar <= 0xDFFFu) return MX_E_ENCODING;
    out->v = scalar;
    return MX_OK;
}

/* Rust Cell::control(v): only a value above the input range becomes control. */
static inline mx_status mx_ctl_make(uint32_t v, mx_ctl *out) {
    if (!out) return MX_E_ARG;
    if (v <= MX_INPUT_MAX) return MX_E_CONTROL;
    out->v = v;
    return MX_OK;
}
static inline mx_ctl mx_ctl_end(void)   { mx_ctl c = { MX_CTRL_END };   return c; }
static inline mx_ctl mx_ctl_frame(void) { mx_ctl c = { MX_CTRL_FRAME }; return c; }

/* Comparisons never leave the type system: a cell compares to a cell or to a
 * trusted ASCII literal, a control to a control. */
static inline bool mx_cell_eq(mx_cell a, mx_cell b)  { return a.v == b.v; }
static inline bool mx_cell_is(mx_cell c, char ascii) { return c.v == (uint32_t)(unsigned char)ascii; }
static inline bool mx_cell_in(mx_cell c, uint32_t lo, uint32_t hi) { return c.v >= lo && c.v <= hi; }
static inline bool mx_ctl_eq(mx_ctl a, mx_ctl b)     { return a.v == b.v; }

/* Mixed stream symbols. */
static inline mx_sym  mx_sym_from_cell(mx_cell c) { mx_sym s = { c.v }; return s; }
static inline mx_sym  mx_sym_from_ctl(mx_ctl c)   { mx_sym s = { c.v }; return s; }
static inline bool    mx_sym_is_input(mx_sym s)   { return s.v <= MX_INPUT_MAX; }   /* Rust is_input */
static inline mx_status mx_sym_cell(mx_sym s, mx_cell *out) {
    if (!out) return MX_E_ARG;
    if (!mx_sym_is_input(s)) return MX_E_CONTROL;
    out->v = s.v; return MX_OK;
}
static inline mx_status mx_sym_ctl(mx_sym s, mx_ctl *out) {
    if (!out) return MX_E_ARG;
    if (mx_sym_is_input(s)) return MX_E_CONTROL;
    out->v = s.v; return MX_OK;
}
/* Rust Cell::raw. For encoders and tests only; never for program logic. */
static inline uint32_t mx_sym_raw(mx_sym s) { return s.v; }

/* ---- untrusted wrappers (Rust Untrusted<T>) ------------------------- */
typedef struct mx_untrusted_bytes { const uint8_t *p; size_t n; } mx_untrusted_bytes;
typedef struct mx_untrusted_u32   { uint32_t v; } mx_untrusted_u32;
typedef struct mx_untrusted_u64   { uint64_t v; } mx_untrusted_u64;

static inline mx_untrusted_bytes mx_untrusted_bytes_new(const void *p, size_t n) {
    mx_untrusted_bytes u = { (const uint8_t *)p, n }; return u;
}
static inline mx_untrusted_u32 mx_untrusted_u32_new(uint32_t v) { mx_untrusted_u32 u = { v }; return u; }
static inline mx_untrusted_u64 mx_untrusted_u64_new(uint64_t v) { mx_untrusted_u64 u = { v }; return u; }

/* Rust Untrusted::validate with a range checker: the only way to a plain integer. */
static inline mx_status mx_validate_u32_range(mx_untrusted_u32 u, uint32_t lo, uint32_t hi, uint32_t *out) {
    if (!out) return MX_E_ARG;
    if (u.v < lo || u.v > hi) return MX_E_RANGE;
    *out = u.v; return MX_OK;
}
static inline mx_status mx_validate_u64_range(mx_untrusted_u64 u, uint64_t lo, uint64_t hi, uint64_t *out) {
    if (!out) return MX_E_ARG;
    if (u.v < lo || u.v > hi) return MX_E_RANGE;
    *out = u.v; return MX_OK;
}
/* A length or offset from input, checked against the real backing size (security review finding 6). */
static inline mx_status mx_validate_len(mx_untrusted_u64 len, uint64_t offset, uint64_t backing, uint64_t *out) {
    if (!out) return MX_E_ARG;
    if (offset > backing || len.v > backing - offset) return MX_E_RANGE;
    *out = len.v; return MX_OK;
}

/* ---- frames: receiver-owned, out-of-band length ---------------------- */
/* The receiver allocates `cells` with `cap` slots; `len` is written only by
 * the widening functions. Message boundaries travel here, never in-band. */
typedef struct mx_frame {
    mx_sym  *cells;
    uint32_t cap;      /* receiver's ceiling, in symbols, including the terminator */
    uint32_t len;      /* symbols written, including the terminator           */
} mx_frame;

static inline mx_frame mx_frame_init(mx_sym *storage, uint32_t cap) {
    mx_frame f = { storage, cap, 0 }; return f;
}

/* Rust widen(bytes, max): one cell per byte, then MX_CTRL_END. Fails with
 * MX_E_BUDGET when n > max or the frame cannot hold n + 1 symbols. Nothing is
 * written on failure. */
mx_status mx_widen_bytes(mx_untrusted_bytes in, size_t max, mx_frame *out);

/* UTF-8 widening: one cell per scalar value, then MX_CTRL_END. Rejects
 * overlong forms, surrogates, values above 0x10FFFF and truncated sequences
 * (MX_E_ENCODING; `*consumed` says how many bytes were valid). Same budget
 * rule as mx_widen_bytes (max counts input bytes). */
mx_status mx_widen_utf8(mx_untrusted_bytes in, size_t max, mx_frame *out, size_t *consumed);

/* Number of input cells before the first control symbol (excludes it). */
uint32_t mx_frame_input_len(const mx_frame *f);

/* ---- validators: the only exits from input space --------------------- */

/* Decimal unsigned integer made of the frame's input cells, within [lo, hi].
 * Rejects empty, non-digit, leading '+'/'-', and overflow (MX_E_SYNTAX / MX_E_RANGE). */
mx_status mx_validate_u32_dec(const mx_frame *f, uint32_t lo, uint32_t hi, uint32_t *out);

/* Copy the frame's input cells into a trusted C string when every cell is in
 * `allowed` (a NUL-terminated set of ASCII bytes), 1..cap-1 cells long. */
mx_status mx_validate_token(const mx_frame *f, const char *allowed, char *out, size_t cap);

/* One path segment: [A-Za-z0-9._-]{1,255}, not "." or "..". */
mx_status mx_validate_path_segment(const mx_frame *f, char *out, size_t cap);

/* Exact match against a trusted table of names; writes the index. */
mx_status mx_validate_enum(const mx_frame *f, const char *const *names, size_t nnames, size_t *index);

/* Re-encode the frame's scalars as UTF-8 text with C0 controls other than
 * \t and \n refused (MX_E_SYNTAX). Output is always valid UTF-8, NUL-terminated. */
mx_status mx_validate_text(const mx_frame *f, char *out, size_t cap, size_t *outlen);

/* Name for a status code, for logs. */
const char *mx_status_str(mx_status s);

#ifdef __cplusplus
}
#endif
#endif /* MX_INPUT_H */
