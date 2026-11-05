/* test_vectors.c -- run tests/vectors/cells.vec against mx_input.h.
 * Exit 0 when every line agrees, 1 otherwise. */
#include "mx_input.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails = 0, lines = 0;
#define FAIL(...) do { fprintf(stderr, "vec line %d: ", lineno); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); fails++; } while (0)

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
static size_t parse_bytes(const char *s, uint8_t *out, size_t cap) {
    if (strcmp(s, "-") == 0) return 0;
    size_t n = 0;
    for (; s[0] && s[1]; s += 2) {
        int a = hexval(s[0]), b = hexval(s[1]);
        if (a < 0 || b < 0 || n >= cap) return (size_t)-1;
        out[n++] = (uint8_t)(a * 16 + b);
    }
    return s[0] ? (size_t)-1 : n;
}
static bool parse_cells(char **toks, int ntoks, uint32_t *out, int *n) {
    *n = 0;
    for (int i = 0; i < ntoks; i++) out[(*n)++] = (uint32_t)strtoul(toks[i], NULL, 16);
    return true;
}
static void check_cells(int lineno, const mx_frame *f, uint32_t *want, int nwant) {
    if ((int)f->len != nwant) { FAIL("len %u != %d", f->len, nwant); return; }
    for (int i = 0; i < nwant; i++)
        if (mx_sym_raw(f->cells[i]) != want[i]) { FAIL("cell %d: %x != %x", i, mx_sym_raw(f->cells[i]), want[i]); return; }
    /* invariant: exactly one control symbol, at the end */
    for (int i = 0; i + 1 < nwant; i++) if (!mx_sym_is_input(f->cells[i])) FAIL("control symbol inside input at %d", i);
    if (mx_sym_is_input(f->cells[nwant - 1])) FAIL("no terminator");
}

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "tests/vectors/cells.vec";
    FILE *fp = fopen(path, "r");
    if (!fp) { perror(path); return 2; }
    char line[1024];
    int lineno = 0;
    while (fgets(line, sizeof line, fp)) {
        lineno++;
        char *hash = strchr(line, '#'); if (hash) *hash = 0;
        char *toks[64]; int nt = 0;
        for (char *t = strtok(line, " \t\r\n"); t && nt < 64; t = strtok(NULL, " \t\r\n")) toks[nt++] = t;
        if (nt == 0) continue;
        lines++;
        int arrow = -1; for (int i = 0; i < nt; i++) if (strcmp(toks[i], "->") == 0) arrow = i;
        if (arrow < 0 || arrow + 1 >= nt) { FAIL("malformed"); continue; }
        char **rhs = toks + arrow + 1; int nrhs = nt - arrow - 1;
        uint8_t bytes[256]; uint32_t want[300]; int nwant;
        mx_sym storage[300]; mx_frame f = mx_frame_init(storage, 300);

        if (strcmp(toks[0], "byte") == 0) {
            mx_cell c = mx_cell_from_byte((uint8_t)strtoul(toks[1], NULL, 16));
            if (strcmp(rhs[0], "cell") != 0 || c.v != strtoul(rhs[1], NULL, 16)) FAIL("byte -> %x", c.v);
            if (!mx_sym_is_input(mx_sym_from_cell(c))) FAIL("byte cell not input");
        } else if (strcmp(toks[0], "scalar") == 0) {
            mx_cell c; mx_status s = mx_cell_from_scalar((uint32_t)strtoul(toks[1], NULL, 16), &c);
            if (strcmp(rhs[0], "ok") == 0) { if (s != MX_OK || c.v != strtoul(rhs[1], NULL, 16)) FAIL("scalar: %s", mx_status_str(s)); }
            else if (s == MX_OK) FAIL("scalar accepted %x", c.v);
        } else if (strcmp(toks[0], "control") == 0) {
            mx_ctl c; mx_status s = mx_ctl_make((uint32_t)strtoul(toks[1], NULL, 16), &c);
            if ((strcmp(rhs[0], "ok") == 0) != (s == MX_OK)) FAIL("control: %s", mx_status_str(s));
            if (s == MX_OK && mx_sym_is_input(mx_sym_from_ctl(c))) FAIL("control value reads as input");
        } else if (strcmp(toks[0], "widen") == 0 || strcmp(toks[0], "utf8") == 0) {
            size_t n = parse_bytes(toks[1], bytes, sizeof bytes);
            if (n == (size_t)-1) { FAIL("bad bytes"); continue; }
            size_t max = strtoul(toks[2] + 4, NULL, 10);
            size_t consumed = 0; mx_status s;
            if (toks[0][0] == 'w') s = mx_widen_bytes(mx_untrusted_bytes_new(bytes, n), max, &f);
            else s = mx_widen_utf8(mx_untrusted_bytes_new(bytes, n), max, &f, &consumed);
            if (strcmp(rhs[0], "ok") == 0) {
                if (s != MX_OK) { FAIL("want ok got %s", mx_status_str(s)); continue; }
                parse_cells(rhs + 1, nrhs - 1, want, &nwant);
                check_cells(lineno, &f, want, nwant);
            } else if (strcmp(rhs[0], "budget") == 0) {
                if (s != MX_E_BUDGET) FAIL("want budget got %s", mx_status_str(s));
            } else if (strcmp(rhs[0], "encoding") == 0) {
                if (s != MX_E_ENCODING) FAIL("want encoding got %s", mx_status_str(s));
                else if (consumed != strtoul(rhs[1], NULL, 10)) FAIL("consumed %zu != %s", consumed, rhs[1]);
            } else FAIL("unknown outcome %s", rhs[0]);
            if (s != MX_OK && f.len != 0) FAIL("frame written on failure");
        } else FAIL("unknown op %s", toks[0]);
    }
    fclose(fp);
    printf("cells.vec (C): %d vectors, %d failures\n", lines, fails);
    return fails ? 1 : 0;
}
