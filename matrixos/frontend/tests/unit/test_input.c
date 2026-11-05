/* test_input.c -- unit tests and a deterministic fuzz loop for mx_input.
 * Build with -fsanitize=address,undefined; every check is an invariant the
 * design doc states. Exit 0 on success. */
#include "mx_input.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails = 0;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #x); fails++; } } while (0)

static uint64_t rng = 0xF00DCAFE12345678ULL;
static uint64_t xs(void) { uint64_t x = rng; x ^= x << 13; x ^= x >> 7; x ^= x << 17; rng = x; return x; }

static void test_cells(void) {
    for (int b = 0; b <= 255; b++) {
        mx_cell c = mx_cell_from_byte((uint8_t)b);
        CHECK(mx_sym_is_input(mx_sym_from_cell(c)));
        CHECK(c.v != MX_CTRL_END && c.v != MX_CTRL_FRAME);
    }
    /* every scalar value is input; every surrogate and every value above is refused */
    for (uint32_t v = 0; v <= 0x110010u; v++) {
        mx_cell c; mx_status s = mx_cell_from_scalar(v, &c);
        bool valid = v <= MX_INPUT_MAX && !(v >= 0xD800u && v <= 0xDFFFu);
        CHECK((s == MX_OK) == valid);
        if (s == MX_OK) CHECK(mx_sym_is_input(mx_sym_from_cell(c)));
        mx_ctl t; mx_status ts = mx_ctl_make(v, &t);
        CHECK((ts == MX_OK) == (v > MX_INPUT_MAX));
        if (ts == MX_OK) CHECK(!mx_sym_is_input(mx_sym_from_ctl(t)));
    }
    mx_sym s = mx_sym_from_ctl(mx_ctl_end());
    mx_cell c; mx_ctl t;
    CHECK(mx_sym_cell(s, &c) == MX_E_CONTROL);
    CHECK(mx_sym_ctl(s, &t) == MX_OK && mx_ctl_eq(t, mx_ctl_end()));
    CHECK(mx_cell_from_scalar(0x41, NULL) == MX_E_ARG);
}

static void test_widen(void) {
    mx_sym st[8]; mx_frame f = mx_frame_init(st, 8);
    uint8_t evil[] = { 0x00, 0xFF, '\n', 0x1A, 0x04 };
    CHECK(mx_widen_bytes(mx_untrusted_bytes_new(evil, 5), 16, &f) == MX_OK);
    CHECK(f.len == 6 && mx_frame_input_len(&f) == 5);
    int ctl = 0; for (uint32_t i = 0; i < f.len; i++) if (!mx_sym_is_input(f.cells[i])) ctl++;
    CHECK(ctl == 1 && !mx_sym_is_input(f.cells[5]));
    /* receiver ceiling: max */
    uint8_t z[17] = {0};
    CHECK(mx_widen_bytes(mx_untrusted_bytes_new(z, 17), 16, &f) == MX_E_BUDGET);
    /* receiver ceiling: frame capacity (n + 1 must fit) */
    mx_frame small = mx_frame_init(st, 5);
    CHECK(mx_widen_bytes(mx_untrusted_bytes_new(evil, 5), 16, &small) == MX_E_BUDGET);
    CHECK(small.len == 0);
    CHECK(mx_widen_bytes(mx_untrusted_bytes_new(evil, 4), 16, &small) == MX_OK && small.len == 5);
    /* zero-length input: just the terminator */
    CHECK(mx_widen_bytes(mx_untrusted_bytes_new(NULL, 0), 0, &f) == MX_OK && f.len == 1 && !mx_sym_is_input(f.cells[0]));
    /* NULL frame */
    CHECK(mx_widen_bytes(mx_untrusted_bytes_new(evil, 5), 16, NULL) == MX_E_ARG);
    /* utf8 capacity: 2 scalars need 3 slots */
    mx_frame two = mx_frame_init(st, 2);
    size_t consumed = 0;
    CHECK(mx_widen_utf8(mx_untrusted_bytes_new((const uint8_t *)"ab", 2), 8, &two, &consumed) == MX_E_BUDGET);
    CHECK(two.len == 0);
}

static void test_validators(void) {
    mx_sym st[64]; mx_frame f = mx_frame_init(st, 64);
    uint32_t u = 0; char buf[32]; size_t idx = 99, outlen = 0;
    #define LOAD(s) CHECK(mx_widen_bytes(mx_untrusted_bytes_new((s), strlen(s)), 63, &f) == MX_OK)

    LOAD("12345");  CHECK(mx_validate_u32_dec(&f, 0, 100000, &u) == MX_OK && u == 12345);
    LOAD("12345");  CHECK(mx_validate_u32_dec(&f, 0, 1000, &u) == MX_E_RANGE);
    LOAD("4294967295"); CHECK(mx_validate_u32_dec(&f, 0, 0xFFFFFFFFu, &u) == MX_OK && u == 0xFFFFFFFFu);
    LOAD("4294967296"); CHECK(mx_validate_u32_dec(&f, 0, 0xFFFFFFFFu, &u) == MX_E_RANGE);
    LOAD("99999999999"); CHECK(mx_validate_u32_dec(&f, 0, 0xFFFFFFFFu, &u) == MX_E_SYNTAX);
    LOAD("");       CHECK(mx_validate_u32_dec(&f, 0, 10, &u) == MX_E_SYNTAX);
    LOAD("-1");     CHECK(mx_validate_u32_dec(&f, 0, 10, &u) == MX_E_SYNTAX);
    LOAD("+1");     CHECK(mx_validate_u32_dec(&f, 0, 10, &u) == MX_E_SYNTAX);
    LOAD("1 ");     CHECK(mx_validate_u32_dec(&f, 0, 10, &u) == MX_E_SYNTAX);
    LOAD("0x10");   CHECK(mx_validate_u32_dec(&f, 0, 100, &u) == MX_E_SYNTAX);

    LOAD("hello_world-1.txt"); CHECK(mx_validate_path_segment(&f, buf, sizeof buf) == MX_OK && strcmp(buf, "hello_world-1.txt") == 0);
    LOAD("..");       CHECK(mx_validate_path_segment(&f, buf, sizeof buf) == MX_E_SYNTAX && buf[0] == 0);
    LOAD(".");        CHECK(mx_validate_path_segment(&f, buf, sizeof buf) == MX_E_SYNTAX);
    LOAD("a/b");      CHECK(mx_validate_path_segment(&f, buf, sizeof buf) == MX_E_SYNTAX);
    LOAD("a\\b");     CHECK(mx_validate_path_segment(&f, buf, sizeof buf) == MX_E_SYNTAX);
    LOAD("");         CHECK(mx_validate_path_segment(&f, buf, sizeof buf) == MX_E_SYNTAX);
    LOAD("abcdefghij"); CHECK(mx_validate_path_segment(&f, buf, 10) == MX_E_BUDGET);  /* needs 11 incl NUL */
    LOAD("abcdefghij"); CHECK(mx_validate_path_segment(&f, buf, 11) == MX_OK);
    /* a NUL cell must never match the allowed set's terminator */
    { uint8_t nul[1] = {0}; CHECK(mx_widen_bytes(mx_untrusted_bytes_new(nul, 1), 63, &f) == MX_OK);
      CHECK(mx_validate_token(&f, "abc", buf, sizeof buf) == MX_E_SYNTAX); }

    static const char *const names[] = { "read", "write", "render" };
    LOAD("write");  CHECK(mx_validate_enum(&f, names, 3, &idx) == MX_OK && idx == 1);
    LOAD("writ");   CHECK(mx_validate_enum(&f, names, 3, &idx) == MX_E_SYNTAX);
    LOAD("writer"); CHECK(mx_validate_enum(&f, names, 3, &idx) == MX_E_SYNTAX);
    LOAD("WRITE");  CHECK(mx_validate_enum(&f, names, 3, &idx) == MX_E_SYNTAX);

    /* text: round-trips valid UTF-8, refuses C0 controls other than \t \n */
    const char *txt = "h\xc3\xa9llo\n\xe2\x82\xac\xf0\x9f\x98\x80";
    size_t consumed;
    CHECK(mx_widen_utf8(mx_untrusted_bytes_new((const uint8_t *)txt, strlen(txt)), 63, &f, &consumed) == MX_OK);
    CHECK(mx_validate_text(&f, buf, sizeof buf, &outlen) == MX_OK && outlen == strlen(txt) && memcmp(buf, txt, outlen) == 0);
    CHECK(mx_validate_text(&f, buf, 5, &outlen) == MX_E_BUDGET);
    LOAD("a\x01b"); CHECK(mx_validate_text(&f, buf, sizeof buf, &outlen) == MX_E_SYNTAX);
    LOAD("a\x7f");  CHECK(mx_validate_text(&f, buf, sizeof buf, &outlen) == MX_E_SYNTAX);

    /* lengths from input are checked against the real backing size */
    uint64_t len;
    CHECK(mx_validate_len(mx_untrusted_u64_new(16), 0, 16, &len) == MX_OK && len == 16);
    CHECK(mx_validate_len(mx_untrusted_u64_new(17), 0, 16, &len) == MX_E_RANGE);
    CHECK(mx_validate_len(mx_untrusted_u64_new(1), 16, 16, &len) == MX_OK);
    CHECK(mx_validate_len(mx_untrusted_u64_new(1), 17, 16, &len) == MX_E_RANGE);
    CHECK(mx_validate_len(mx_untrusted_u64_new(UINT64_MAX), 1, 16, &len) == MX_E_RANGE); /* no overflow */
    uint32_t r;
    CHECK(mx_validate_u32_range(mx_untrusted_u32_new(5), 1, 5, &r) == MX_OK && r == 5);
    CHECK(mx_validate_u32_range(mx_untrusted_u32_new(6), 1, 5, &r) == MX_E_RANGE);
    #undef LOAD
}

/* Fuzz: random bytes through every entry point; the invariants must hold and
 * nothing may crash (run under ASan/UBSan). */
static void fuzz(unsigned iters) {
    uint8_t in[300]; mx_sym st[128]; char out[64];
    for (unsigned it = 0; it < iters; it++) {
        size_t n = (size_t)(xs() % sizeof in);
        int mode = (int)(xs() % 4);
        for (size_t i = 0; i < n; i++) {
            uint64_t r = xs();
            in[i] = mode == 0 ? (uint8_t)r : mode == 1 ? (uint8_t)("0123456789"[r % 10]) :
                    mode == 2 ? (uint8_t)(0x80 | (r & 0x7F)) : (uint8_t)("aZ09._-/\\\n\t\x01\xc3\xa9\xf0\x9f\x98\x80"[r % 19]);
        }
        uint32_t cap = 1 + (uint32_t)(xs() % 128);
        size_t max = (size_t)(xs() % 400);
        mx_frame f = mx_frame_init(st, cap);
        mx_status s = (xs() & 1) ? mx_widen_bytes(mx_untrusted_bytes_new(in, n), max, &f)
                                 : mx_widen_utf8(mx_untrusted_bytes_new(in, n), max, &f, NULL);
        if (s != MX_OK) { CHECK(f.len == 0); continue; }
        CHECK(f.len >= 1 && f.len <= cap);
        for (uint32_t i = 0; i + 1 < f.len; i++) CHECK(mx_sym_is_input(f.cells[i]));
        CHECK(!mx_sym_is_input(f.cells[f.len - 1]));
        CHECK(mx_frame_input_len(&f) == f.len - 1);
        uint32_t u; char tok[64]; size_t idx, ol;
        static const char *const names[] = { "read", "write" };
        if (mx_validate_u32_dec(&f, 0, 1000, &u) == MX_OK) CHECK(u <= 1000);
        size_t tcap = 1 + (size_t)(xs() % 64);
        if (mx_validate_path_segment(&f, tok, tcap) == MX_OK) {
            CHECK(strlen(tok) < tcap && strlen(tok) == f.len - 1);
            CHECK(!strchr(tok, '/') && strcmp(tok, "..") != 0 && strcmp(tok, ".") != 0);
        }
        if (mx_validate_enum(&f, names, 2, &idx) == MX_OK) CHECK(idx < 2);
        if (mx_validate_text(&f, out, tcap, &ol) == MX_OK) CHECK(ol < tcap && out[ol] == 0);
    }
}

int main(void) {
    test_cells();
    test_widen();
    test_validators();
    fuzz(200000);
    printf("test_input: %s (%d failures)\n", fails ? "FAIL" : "ok", fails);
    return fails ? 1 : 0;
}
