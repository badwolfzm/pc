/*
 * mxlift_hook -- source-level LFSR / GF(2) recurrence lifting for the
 * MatrixOS clang toolchain.
 *
 *   mxlift_hook in.c -o out.c [--clang /usr/bin/clang] [--mx-lift-include "mx_lift.h"]
 *               [--report] [--dry-run] [-- -I... -D... -U... -std=... -isystem ...]
 *
 * Pipeline
 *   1. clang -fsyntax-only -Xclang -ast-dump=json  (absolute path, no shell,
 *      only an allowlist of flags is forwarded: security review finding 3)
 *   2. for every for/while loop in the main file whose body only rewrites
 *      ONE unsigned integer variable x (8/16/32/64 bit) with shifts, xor,
 *      and/or with constants, not, the -(x & 1) & POLY tap select in any of
 *      its spellings, or multiply by a constant, and whose trip count is a
 *      loop-invariant expression, lower the body to mxl_insn[] and call
 *      mxl_lift() from matrixos/lfsr/mx_lift.h.
 *   3. MXL_OK: replace the loop text with one mx_aff_jump() call on a
 *      `static const mx_aff` table (lands in .rodata) and the loop
 *      counter's final value.  Anything else: leave the loop untouched and
 *      say why.
 *
 * The hook never guesses: a loop is lifted only when the lifter proves the
 * body affine, and the lowering refuses anything whose C semantics it
 * cannot reproduce exactly at the state width (integer promotion of
 * 8/16-bit state, signed types, volatile, side effects, other variables).
 *
 * Exit: 0 ok (output written, even when nothing was lifted), 1 clang or
 * parse failure, 2 usage.
 */
#define _GNU_SOURCE
#include "mxl_json.h"
#include "mx_lift.h"
#include <stdarg.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>

/* ------------------------------------------------------------------------ */
/* utilities                                                                 */
/* ------------------------------------------------------------------------ */
static char *xstrdup(const char *s) { size_t n = strlen(s) + 1; char *d = malloc(n); memcpy(d, s, n); return d; }
static char *read_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return NULL; }
    char *b = malloc((size_t)n + 1); size_t rd = fread(b, 1, (size_t)n, f); fclose(f);
    if (rd != (size_t)n) { free(b); return NULL; }
    b[n] = 0; if (len) *len = (size_t)n; return b;
}
typedef struct { char *s; size_t n, cap; } sbuf;
static void sb_put(sbuf *b, const char *s, size_t n) {
    if (b->n + n + 1 > b->cap) { b->cap = (b->n + n + 1) * 2 + 64; b->s = realloc(b->s, b->cap); }
    memcpy(b->s + b->n, s, n); b->n += n; b->s[b->n] = 0;
}
static void sb_printf(sbuf *b, const char *fmt, ...) {
    char tmp[4096]; va_list ap; va_start(ap, fmt); int n = vsnprintf(tmp, sizeof tmp, fmt, ap); va_end(ap);
    if (n > 0) sb_put(b, tmp, (size_t)n < sizeof tmp ? (size_t)n : sizeof tmp - 1);
}

/* ------------------------------------------------------------------------ */
/* running clang                                                             */
/* ------------------------------------------------------------------------ */
static bool flag_allowed(const char *f, bool *takes_arg) {
    *takes_arg = false;
    static const char *const prefix_ok[] = { "-I", "-D", "-U", "-std=", "-W", "-f", "-m" , "-O"};
    static const char *const bad_f[] = { "-fplugin", "-fpass-plugin", "-fprofile", "-fmodule", "-fsanitize-blacklist", "-fsanitize-ignorelist", "-fdebug-prefix-map", "-ffile-prefix-map" };
    if (!strcmp(f, "-isystem") || !strcmp(f, "-I") || !strcmp(f, "-D") || !strcmp(f, "-U") || !strcmp(f, "-include") || !strcmp(f, "-idirafter") || !strcmp(f, "-iquote")) { *takes_arg = true; return true; }
    for (size_t i = 0; i < sizeof bad_f / sizeof bad_f[0]; ++i) if (!strncmp(f, bad_f[i], strlen(bad_f[i]))) return false;
    if (!strncmp(f, "-Xclang", 7) || f[0] == '@' || !strncmp(f, "-specs", 6) || !strncmp(f, "-B", 2) || !strcmp(f, "-o") || !strncmp(f, "-x", 2)) return false;
    for (size_t i = 0; i < sizeof prefix_ok / sizeof prefix_ok[0]; ++i) if (!strncmp(f, prefix_ok[i], strlen(prefix_ok[i]))) return true;
    return false;
}
static int run_clang(const char *clang, const char *src, char **extra, int nextra, const char *json_out) {
    const char *argv[256]; int n = 0;
    argv[n++] = clang; argv[n++] = "-fsyntax-only"; argv[n++] = "-w"; argv[n++] = "-Xclang"; argv[n++] = "-ast-dump=json";
    for (int i = 0; i < nextra && n < 250; ++i) {
        bool ta; if (!flag_allowed(extra[i], &ta)) { fprintf(stderr, "mxlift_hook: flag not allowed: %s\n", extra[i]); return -2; }
        argv[n++] = extra[i]; if (ta) { if (i + 1 >= nextra) return -2; argv[n++] = extra[++i]; }
    }
    argv[n++] = src; argv[n] = NULL;
    pid_t pid = fork(); if (pid < 0) return -1;
    if (pid == 0) {
        int fd = open(json_out, O_WRONLY | O_CREAT | O_TRUNC, 0600); if (fd < 0) _exit(126);
        dup2(fd, 1); close(fd);
        execv(argv[0], (char *const *)argv); _exit(127);
    }
    int st; waitpid(pid, &st, 0);
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

/* ------------------------------------------------------------------------ */
/* AST helpers                                                               */
/* ------------------------------------------------------------------------ */
typedef struct { long long begin, end; } srange;   /* [begin, end) byte offsets */
static bool node_range(const jval *n, srange *r) {
    jval *rg = jget(n, "range"); if (!rg) return false;
    jval *b = jget(rg, "begin"), *e = jget(rg, "end");
    long long bo, eo, tl = 1;
    /* clang elides fields equal to the previous location in some nodes; expansionLoc/spellingLoc hold macro cases */
    jval *bb = jget(b, "expansionLoc") ? jget(b, "expansionLoc") : b;
    jval *ee = jget(e, "expansionLoc") ? jget(e, "expansionLoc") : e;
    if (!jhas_num(bb, "offset", &bo) || !jhas_num(ee, "offset", &eo)) return false;
    jhas_num(ee, "tokLen", &tl);
    r->begin = bo; r->end = eo + tl; return true;
}
static const jval *strip(const jval *e) {
    for (;;) {
        const char *k = jkind(e);
        if (!strcmp(k, "ParenExpr") || !strcmp(k, "ImplicitCastExpr") || !strcmp(k, "ConstantExpr")) { const jval *c = jinner_at(e, 0); if (!c) return e; e = c; continue; }
        return e;
    }
}
/* type descriptor */
typedef struct { unsigned width; bool is_signed, ok, is_volatile; } ctype;
static const char *node_type(const jval *n) {
    jval *t = jget(n, "type"); if (!t) return NULL;
    const char *d = jstr(t, "desugaredQualType"); return d ? d : jstr(t, "qualType");
}
static ctype parse_ctype(const char *s) {
    ctype t = {0, false, false, false};
    if (!s) return t;
    char buf[256]; snprintf(buf, sizeof buf, "%s", s);
    /* strip qualifiers */
    char *p = buf; bool vol = false;
    for (;;) {
        while (*p == ' ') ++p;
        if (!strncmp(p, "const ", 6)) { p += 6; continue; }
        if (!strncmp(p, "volatile ", 9)) { p += 9; vol = true; continue; }
        break;
    }
    size_t l = strlen(p); while (l && p[l-1] == ' ') p[--l] = 0;
    static const struct { const char *n; unsigned w; bool s; } tb[] = {
        {"unsigned char", 8, false}, {"signed char", 8, true}, {"char", 8, true},
        {"unsigned short", 16, false}, {"short", 16, true},
        {"unsigned int", 32, false}, {"int", 32, true},
        {"unsigned long", 64, false}, {"long", 64, true},
        {"unsigned long long", 64, false}, {"long long", 64, true},
        {"__uint128_t", 0, false}, {"_Bool", 1, false},
    };
    for (size_t i = 0; i < sizeof tb / sizeof tb[0]; ++i) if (!strcmp(p, tb[i].n)) { t.width = tb[i].w; t.is_signed = tb[i].s; t.ok = tb[i].w >= 8; t.is_volatile = vol; return t; }
    return t;
}
static const char *decl_name(const jval *dre) { jval *rd = jget(dre, "referencedDecl"); return rd ? jstr(rd, "name") : NULL; }
static const char *decl_id(const jval *dre) { jval *rd = jget(dre, "referencedDecl"); return rd ? jstr(rd, "id") : NULL; }
static bool is_declref_to(const jval *e, const char *id) {
    e = strip(e);
    if (strcmp(jkind(e), "DeclRefExpr")) return false;
    const char *d = decl_id(e); return d && id && !strcmp(d, id);
}
/* does the subtree reference the declaration `id` anywhere? */
static bool mentions(const jval *e, const char *id) {
    if (!e) return false;
    if (!strcmp(jkind(e), "DeclRefExpr")) { const char *d = decl_id(e); return d && !strcmp(d, id); }
    for (int i = 0; i < jinner_n(e); ++i) if (mentions(jinner_at(e, i), id)) return true;
    return false;
}
/* side-effect-free, call-free expression over variables and constants */
static bool pure_expr(const jval *e) {
    if (!e) return false;
    const char *k = jkind(e);
    if (!strcmp(k, "DeclRefExpr") || !strcmp(k, "IntegerLiteral") || !strcmp(k, "CharacterLiteral") || !strcmp(k, "UnaryExprOrTypeTraitExpr")) return true;
    if (!strcmp(k, "ParenExpr") || !strcmp(k, "ImplicitCastExpr") || !strcmp(k, "CStyleCastExpr") || !strcmp(k, "ConstantExpr")) return pure_expr(jinner_at(e, 0));
    if (!strcmp(k, "BinaryOperator")) {
        const char *op = jstr(e, "opcode");
        static const char *const ok[] = {"+","-","*","/","%","<<",">>","&","|","^","<",">","<=",">=","==","!=","&&","||"};
        bool f = false; for (size_t i = 0; i < sizeof ok / sizeof ok[0]; ++i) if (op && !strcmp(op, ok[i])) f = true;
        return f && pure_expr(jinner_at(e, 0)) && pure_expr(jinner_at(e, 1));
    }
    if (!strcmp(k, "UnaryOperator")) { const char *op = jstr(e, "opcode"); return op && (!strcmp(op, "-") || !strcmp(op, "~") || !strcmp(op, "!") || !strcmp(op, "+")) && pure_expr(jinner_at(e, 0)); }
    if (!strcmp(k, "ConditionalOperator")) return pure_expr(jinner_at(e, 0)) && pure_expr(jinner_at(e, 1)) && pure_expr(jinner_at(e, 2));
    return false;
}
/* compile-time constant evaluation (literals, -, ~, ( ), and a few binary ops) */
static bool const_eval(const jval *e, uint64_t *out) {
    e = strip(e);
    const char *k = jkind(e);
    if (!strcmp(k, "IntegerLiteral") || !strcmp(k, "CharacterLiteral")) { const char *v = jstr(e, "value"); if (!v) { long long n; if (!jhas_num(e, "value", &n)) return false; *out = (uint64_t)n; return true; } *out = strtoull(v, NULL, 10); return true; }
    if (!strcmp(k, "CStyleCastExpr")) { ctype t = parse_ctype(node_type(e)); uint64_t v; if (!t.ok || !const_eval(jinner_at(e, 0), &v)) return false; *out = v & mx_lift_mask(t.width); return true; }
    if (!strcmp(k, "UnaryOperator")) {
        const char *op = jstr(e, "opcode"); uint64_t v; if (!const_eval(jinner_at(e, 0), &v)) return false;
        ctype t = parse_ctype(node_type(e)); uint64_t m = t.ok ? mx_lift_mask(t.width) : ~0ull;
        if (!strcmp(op, "-")) { *out = (0 - v) & m; return true; }
        if (!strcmp(op, "~")) { *out = (~v) & m; return true; }
        if (!strcmp(op, "+")) { *out = v; return true; }
        return false;
    }
    if (!strcmp(k, "BinaryOperator")) {
        const char *op = jstr(e, "opcode"); uint64_t a, b;
        if (!const_eval(jinner_at(e, 0), &a) || !const_eval(jinner_at(e, 1), &b)) return false;
        ctype t = parse_ctype(node_type(e)); uint64_t m = t.ok ? mx_lift_mask(t.width) : ~0ull;
        if (!strcmp(op, "<<")) { if (b >= 64) return false; *out = (a << b) & m; return true; }
        if (!strcmp(op, ">>")) { if (b >= 64) return false; *out = (a & m) >> b; return true; }
        if (!strcmp(op, "|")) { *out = (a | b) & m; return true; }
        if (!strcmp(op, "&")) { *out = (a & b) & m; return true; }
        if (!strcmp(op, "^")) { *out = (a ^ b) & m; return true; }
        if (!strcmp(op, "+")) { *out = (a + b) & m; return true; }
        if (!strcmp(op, "-")) { *out = (a - b) & m; return true; }
        if (!strcmp(op, "*")) { *out = (a * b) & m; return true; }
        return false;
    }
    return false;
}

/* ------------------------------------------------------------------------ */
/* lowering of one loop body to mxl_insn[]                                   */
/* ------------------------------------------------------------------------ */
typedef struct {
    const char *xid; unsigned w;
    mxl_insn code[4096]; int n;
    int next_reg;                 /* register stack top (1..7) */
    char reason[256];
    bool ok;
} lower_ctx;

static void lfail(lower_ctx *c, const char *fmt, ...) {
    if (!c->ok) return;
    c->ok = false;
    va_list ap; va_start(ap, fmt); vsnprintf(c->reason, sizeof c->reason, fmt, ap); va_end(ap);
}
static void emit(lower_ctx *c, mxl_op op, int dst, int src, int src2, unsigned sh, uint64_t imm) {
    if (c->n >= 4096) { lfail(c, "body too long"); return; }
    mxl_insn I = { (uint8_t)op, (uint8_t)dst, (uint8_t)src, (uint8_t)src2, (uint8_t)sh, imm };
    c->code[c->n++] = I;
}
static int alloc_reg(lower_ctx *c) { if (c->next_reg >= MXL_REGS) { lfail(c, "expression too deep (more than %d temporaries)", MXL_REGS - 1); return 1; } return c->next_reg++; }

/* Width hygiene for 8/16-bit state (C promotes to int): a value is "clean"
 * when its bits above w are known zero in the promoted evaluation. */
static bool is_clean(const jval *e, lower_ctx *c);

/* try to see `e` as a single-bit extraction (src >> sh) & 1  /  src & (1<<sh)  /  (src>>sh)&1 != 0 */
static bool bit_extract(const jval *e, const jval **src, unsigned *sh, unsigned w) {
    e = strip(e);
    if (!strcmp(jkind(e), "BinaryOperator")) {
        const char *op = jstr(e, "opcode");
        if (!strcmp(op, ">>")) {           /* src >> (w-1): the top bit on its own */
            uint64_t k; ctype lt = parse_ctype(node_type(jinner_at(e, 0)));
            if (const_eval(jinner_at(e, 1), &k) && k == w - 1 && lt.ok && lt.width == w) { *src = jinner_at(e, 0); *sh = w - 1; return true; }
            return false;
        }
        if (!strcmp(op, "!=")) { uint64_t z; const jval *r = strip(jinner_at(e, 1)); if (const_eval(r, &z) && z == 0) return bit_extract(jinner_at(e, 0), src, sh, w); return false; }
        if (!strcmp(op, "&")) {
            const jval *l = jinner_at(e, 0), *r = jinner_at(e, 1); uint64_t m;
            if (!const_eval(r, &m)) { const jval *t = l; l = r; r = t; if (!const_eval(r, &m)) return false; }
            if (m == 0 || (m & (m - 1))) return false;                     /* single bit */
            unsigned bit = mx_lift_ctz64(m);
            const jval *ls = strip(l);
            if (!strcmp(jkind(ls), "BinaryOperator") && !strcmp(jstr(ls, "opcode"), ">>")) {
                uint64_t k; if (const_eval(jinner_at(ls, 1), &k) && k < w) { *src = jinner_at(ls, 0); *sh = (unsigned)k + bit; return *sh < w; }
            }
            *src = l; *sh = bit; return bit < w;
        }
    }
    return false;
}

static int lower_expr(const jval *e, lower_ctx *c);

/* lower `e` into a fresh register; returns register (or 0 if e is x itself) */
static int lower_expr(const jval *e, lower_ctx *c) {
    if (!c->ok) return 1;
    e = strip(e);
    const char *k = jkind(e);
    uint64_t cv;
    if (const_eval(e, &cv)) { int r = alloc_reg(c); emit(c, MXL_MOV, r, 0, 0, 0, 0); emit(c, MXL_AND_IMM, r, r, 0, 0, 0); emit(c, MXL_XOR_IMM, r, r, 0, 0, cv & mx_lift_mask(c->w)); return r; }
    if (!strcmp(k, "DeclRefExpr")) {
        if (is_declref_to(e, c->xid)) return 0;
        lfail(c, "reference to another variable '%s'", decl_name(e) ? decl_name(e) : "?"); return 1;
    }
    if (!strcmp(k, "CStyleCastExpr")) {
        ctype t = parse_ctype(node_type(e));
        if (!t.ok || t.width < c->w) { lfail(c, "cast to a narrower or unsupported type"); return 1; }
        return lower_expr(jinner_at(e, 0), c);   /* widening/same-width cast of a w-bit value: no-op */
    }
    if (!strcmp(k, "UnaryOperator")) {
        const char *op = jstr(e, "opcode"); const jval *a = jinner_at(e, 0);
        if (!strcmp(op, "~")) { int r = lower_expr(a, c); int d = r ? r : alloc_reg(c); emit(c, MXL_NOT, d, r, 0, 0, 0); return d; }
        if (!strcmp(op, "-")) {
            const jval *src; unsigned sh;
            if (bit_extract(a, &src, &sh, c->w)) { int r = lower_expr(src, c); int d = r ? r : alloc_reg(c); emit(c, MXL_MASKSEL, d, r, 0, sh, mx_lift_mask(c->w)); return d; }
            lfail(c, "unary minus of something other than a single bit"); return 1;
        }
        lfail(c, "unsupported unary operator %s", op); return 1;
    }
    if (!strcmp(k, "ConditionalOperator")) {
        const jval *cond = jinner_at(e, 0), *tv = jinner_at(e, 1), *fv = jinner_at(e, 2);
        const jval *src; unsigned sh; uint64_t A, B;
        if (!bit_extract(cond, &src, &sh, c->w) || !const_eval(tv, &A) || !const_eval(fv, &B)) { lfail(c, "?: that is not (bit ? const : const)"); return 1; }
        int r = lower_expr(src, c); int d = r ? r : alloc_reg(c);
        emit(c, MXL_MASKSEL, d, r, 0, sh, (A ^ B) & mx_lift_mask(c->w));
        if (B & mx_lift_mask(c->w)) emit(c, MXL_XOR_IMM, d, d, 0, 0, B & mx_lift_mask(c->w));
        return d;
    }
    if (!strcmp(k, "BinaryOperator")) {
        const char *op = jstr(e, "opcode"); const jval *l = jinner_at(e, 0), *r = jinner_at(e, 1);
        uint64_t imm;
        if (!strcmp(op, ">>") || !strcmp(op, "<<")) {
            if (!const_eval(r, &imm)) { lfail(c, "shift by a non-constant"); return 1; }
            if (imm >= c->w) { lfail(c, "shift by %llu >= width %u", (unsigned long long)imm, c->w); return 1; }
            ctype lt = parse_ctype(node_type(l));
            if (op[0] == '>' && lt.ok && lt.width > c->w && !is_clean(l, c)) { lfail(c, "right shift of a promoted value whose high bits may be set"); return 1; }
            if (lt.ok && lt.is_signed && lt.width == c->w) { lfail(c, "shift of a signed value"); return 1; }
            int a = lower_expr(l, c); int d = a ? a : alloc_reg(c);
            emit(c, op[0] == '>' ? MXL_SHR : MXL_SHL, d, a, 0, (unsigned)imm, 0); return d;
        }
        if (!strcmp(op, "*")) {
            const jval *src; unsigned sh;
            if (const_eval(r, &imm) && bit_extract(l, &src, &sh, c->w)) { int a = lower_expr(src, c); int d = a ? a : alloc_reg(c); emit(c, MXL_MASKSEL, d, a, 0, sh, imm & mx_lift_mask(c->w)); return d; }
            if (const_eval(l, &imm) && bit_extract(r, &src, &sh, c->w)) { int a = lower_expr(src, c); int d = a ? a : alloc_reg(c); emit(c, MXL_MASKSEL, d, a, 0, sh, imm & mx_lift_mask(c->w)); return d; }
            if (const_eval(r, &imm)) { int a = lower_expr(l, c); int d = a ? a : alloc_reg(c); emit(c, MXL_MUL_IMM, d, a, 0, 0, imm & mx_lift_mask(c->w)); return d; }
            if (const_eval(l, &imm)) { int a = lower_expr(r, c); int d = a ? a : alloc_reg(c); emit(c, MXL_MUL_IMM, d, a, 0, 0, imm & mx_lift_mask(c->w)); return d; }
            lfail(c, "multiply of two non-constants"); return 1;
        }
        if (!strcmp(op, "-")) {
            uint64_t z; const jval *src; unsigned sh;
            if (const_eval(l, &z) && z == 0 && bit_extract(r, &src, &sh, c->w)) { int a = lower_expr(src, c); int d = a ? a : alloc_reg(c); emit(c, MXL_MASKSEL, d, a, 0, sh, mx_lift_mask(c->w)); return d; }
            lfail(c, "subtraction is not affine over GF(2)"); return 1;
        }
        mxl_op bop, iop; bool has_imm_form = true;
        if (!strcmp(op, "^")) { bop = MXL_XOR; iop = MXL_XOR_IMM; }
        else if (!strcmp(op, "&")) { bop = MXL_AND; iop = MXL_AND_IMM; }
        else if (!strcmp(op, "|")) { bop = MXL_OR; iop = MXL_OR; has_imm_form = false; }
        else if (!strcmp(op, "+")) { bop = MXL_ADD; iop = MXL_ADD_IMM; }
        else { lfail(c, "unsupported binary operator %s", op); return 1; }
        if (has_imm_form && const_eval(r, &imm)) { int a = lower_expr(l, c); int d = a ? a : alloc_reg(c); emit(c, iop, d, a, 0, 0, imm & mx_lift_mask(c->w)); return d; }
        if (has_imm_form && const_eval(l, &imm)) { int a = lower_expr(r, c); int d = a ? a : alloc_reg(c); emit(c, iop, d, a, 0, 0, imm & mx_lift_mask(c->w)); return d; }
        int a = lower_expr(l, c); int b = lower_expr(r, c);
        int d = a ? a : (b ? b : alloc_reg(c));
        emit(c, bop, d, a, b, 0, 0);
        if (a && b) c->next_reg--;          /* consumed b */
        return d;
    }
    lfail(c, "unsupported expression kind %s", k); return 1;
}

static bool is_clean(const jval *e, lower_ctx *c) {
    e = strip(e);
    const char *k = jkind(e); uint64_t v;
    if (const_eval(e, &v)) return v <= mx_lift_mask(c->w);
    if (!strcmp(k, "DeclRefExpr")) return is_declref_to(e, c->xid);
    if (!strcmp(k, "CStyleCastExpr")) { ctype t = parse_ctype(node_type(e)); return t.ok && t.width == c->w; }
    if (!strcmp(k, "ConditionalOperator")) { uint64_t A, B; return const_eval(jinner_at(e, 1), &A) && const_eval(jinner_at(e, 2), &B) && A <= mx_lift_mask(c->w) && B <= mx_lift_mask(c->w); }
    if (!strcmp(k, "BinaryOperator")) {
        const char *op = jstr(e, "opcode"); const jval *l = jinner_at(e, 0), *r = jinner_at(e, 1);
        if (!strcmp(op, ">>")) return is_clean(l, c);
        if (!strcmp(op, "&")) return is_clean(l, c) || is_clean(r, c);
        if (!strcmp(op, "^") || !strcmp(op, "|")) return is_clean(l, c) && is_clean(r, c);
        if (!strcmp(op, "*")) { const jval *src; unsigned sh; uint64_t imm; return ((const_eval(r, &imm) && bit_extract(l, &src, &sh, c->w)) || (const_eval(l, &imm) && bit_extract(r, &src, &sh, c->w))) && imm <= mx_lift_mask(c->w); }
        return false;
    }
    return false;
}

/* one statement of the body: x = e | x op= e */
static bool lower_stmt(const jval *s, lower_ctx *c) {
    const char *k = jkind(s);
    const char *op = jstr(s, "opcode");
    if (strcmp(k, "BinaryOperator") && strcmp(k, "CompoundAssignOperator")) { lfail(c, "statement is not an assignment to the state variable (%s)", k); return false; }
    const jval *lhs = jinner_at(s, 0), *rhs = jinner_at(s, 1);
    if (!is_declref_to(lhs, c->xid)) { lfail(c, "assignment to something other than the state variable"); return false; }
    if (!op) { lfail(c, "no opcode"); return false; }
    c->next_reg = 1;
    int r;
    if (!strcmp(op, "=")) { r = lower_expr(rhs, c); }
    else {
        /* x op= e  ==>  x = x op e ; build a synthetic by lowering e then combining with reg 0 */
        char bop[3] = { op[0], op[1] == '=' ? 0 : op[1], 0 };
        if (!strcmp(bop, ">>") || !strcmp(bop, "<<")) {
            uint64_t imm; if (!const_eval(rhs, &imm) || imm >= c->w) { lfail(c, "compound shift by non-constant or >= width"); return false; }
            emit(c, bop[0] == '>' ? MXL_SHR : MXL_SHL, 0, 0, 0, (unsigned)imm, 0); return c->ok;
        }
        mxl_op b2, i2; bool imm_form = true;
        if (!strcmp(bop, "^")) { b2 = MXL_XOR; i2 = MXL_XOR_IMM; }
        else if (!strcmp(bop, "&")) { b2 = MXL_AND; i2 = MXL_AND_IMM; }
        else if (!strcmp(bop, "|")) { b2 = MXL_OR; imm_form = false; i2 = MXL_OR; }
        else if (!strcmp(bop, "+")) { b2 = MXL_ADD; i2 = MXL_ADD_IMM; }
        else if (!strcmp(bop, "*")) { b2 = MXL_MUL_IMM; imm_form = true; i2 = MXL_MUL_IMM; uint64_t imm; if (!const_eval(rhs, &imm)) { lfail(c, "*= by non-constant"); return false; } emit(c, MXL_MUL_IMM, 0, 0, 0, 0, imm & mx_lift_mask(c->w)); return c->ok; }
        else { lfail(c, "unsupported compound operator %s", op); return false; }
        uint64_t imm;
        if (imm_form && const_eval(rhs, &imm)) { emit(c, i2, 0, 0, 0, 0, imm & mx_lift_mask(c->w)); return c->ok; }
        r = lower_expr(rhs, c);
        if (!c->ok) return false;
        emit(c, b2, 0, 0, r, 0, 0);
        return c->ok;
    }
    if (!c->ok) return false;
    if (r != 0) emit(c, MXL_MOV, 0, r, 0, 0, 0);
    return c->ok;
}

/* ------------------------------------------------------------------------ */
/* loop recognition                                                          */
/* ------------------------------------------------------------------------ */
typedef struct {
    const jval *loop;            /* ForStmt / WhileStmt */
    srange range;
    const jval *body;
    const jval *xdecl_ref;       /* a DeclRefExpr to x (for name/type) */
    const char *xid, *xname; ctype xt;
    /* trip count */
    enum { TC_FOR_LT, TC_FOR_LE, TC_FOR_NE, TC_WHILE_DEC } kind;
    const char *iid, *iname; ctype it; bool i_declared_in_for;
    srange a_range, n_range;     /* source text of A (init value) and N (bound); a_range.begin<0 => A is the variable itself */
    bool while_signed_gt0;
    /* lowering result */
    mx_aff step; lower_ctx lc;
    bool lifted; char reason[300];
} loop_info;

static bool first_assign_target(const jval *body, const char **xid, const jval **xref) {
    /* body: CompoundStmt of assignments, or one assignment */
    const jval *s = body;
    if (!strcmp(jkind(body), "CompoundStmt")) { if (jinner_n(body) < 1) return false; s = jinner_at(body, 0); }
    const char *k = jkind(s);
    if (strcmp(k, "BinaryOperator") && strcmp(k, "CompoundAssignOperator")) return false;
    const jval *lhs = strip(jinner_at(s, 0));
    if (strcmp(jkind(lhs), "DeclRefExpr")) return false;
    *xid = decl_id(lhs); *xref = lhs; return *xid != NULL;
}
static const char *get_source_text(const char *src, size_t srclen, srange r, char *buf, size_t n) {
    if (r.begin < 0 || r.end > (long long)srclen || r.end <= r.begin || (size_t)(r.end - r.begin) >= n) return NULL;
    memcpy(buf, src + r.begin, (size_t)(r.end - r.begin)); buf[r.end - r.begin] = 0; return buf;
}

static void analyse_loop(loop_info *L) {
    const jval *n = L->loop;
    const char *k = jkind(n);
    L->lifted = false; L->reason[0] = 0;
    if (!node_range(n, &L->range)) { snprintf(L->reason, sizeof L->reason, "no source range (macro?)"); return; }
    const jval *cond = NULL, *inc = NULL, *init = NULL;
    if (!strcmp(k, "ForStmt")) {
        if (jinner_n(n) != 5) { snprintf(L->reason, sizeof L->reason, "unexpected ForStmt shape"); return; }
        init = jinner_at(n, 0); cond = jinner_at(n, 2); inc = jinner_at(n, 3); L->body = jinner_at(n, 4);
        if (init->t == J_OBJ && init->n == 0) init = NULL;
        if (cond->t == J_OBJ && cond->n == 0) { snprintf(L->reason, sizeof L->reason, "for loop without a condition"); return; }
        if (inc->t == J_OBJ && inc->n == 0) { snprintf(L->reason, sizeof L->reason, "for loop without an increment"); return; }
    } else if (!strcmp(k, "WhileStmt")) {
        if (jinner_n(n) != 2) { snprintf(L->reason, sizeof L->reason, "unexpected WhileStmt shape"); return; }
        cond = jinner_at(n, 0); L->body = jinner_at(n, 1);
    } else { snprintf(L->reason, sizeof L->reason, "not a for/while loop"); return; }

    /* state variable: the target of the first assignment in the body */
    if (!first_assign_target(L->body, &L->xid, &L->xdecl_ref)) { snprintf(L->reason, sizeof L->reason, "body does not start with an assignment to a variable"); return; }
    L->xname = decl_name(L->xdecl_ref);
    L->xt = parse_ctype(node_type(L->xdecl_ref));
    if (!L->xt.ok) { snprintf(L->reason, sizeof L->reason, "state variable '%s' has an unsupported type", L->xname); return; }
    if (L->xt.is_volatile) { snprintf(L->reason, sizeof L->reason, "state variable '%s' is volatile", L->xname); return; }
    if (L->xt.is_signed) { snprintf(L->reason, sizeof L->reason, "state variable '%s' is signed (shifts would be implementation-defined)", L->xname); return; }
    if (!mx_lift_width_ok(L->xt.width)) { snprintf(L->reason, sizeof L->reason, "state width %u not 8/16/32/64", L->xt.width); return; }

    /* trip count */
    if (!strcmp(k, "ForStmt")) {
        const jval *c = strip(cond);
        if (strcmp(jkind(c), "BinaryOperator")) { snprintf(L->reason, sizeof L->reason, "condition is not a comparison"); return; }
        const char *op = jstr(c, "opcode"); const jval *cl = jinner_at(c, 0), *cr = jinner_at(c, 1);
        /* the counter is whatever the increment modifies; then match it to a side of the comparison */
        const jval *incv = strip(inc); const jval *inc_target = NULL;
        if (!strcmp(jkind(incv), "UnaryOperator") || !strcmp(jkind(incv), "CompoundAssignOperator") || !strcmp(jkind(incv), "BinaryOperator")) inc_target = strip(jinner_at(incv, 0));
        if (!inc_target || strcmp(jkind(inc_target), "DeclRefExpr")) { snprintf(L->reason, sizeof L->reason, "increment does not modify a variable"); return; }
        const char *inc_id = decl_id(inc_target);
        /* i on the left: i < N, i <= N, i != N; or N > i, N >= i */
        const jval *iref = strip(cl), *bound = cr;
        if (!strcmp(jkind(iref), "DeclRefExpr") && inc_id && decl_id(iref) && !strcmp(decl_id(iref), inc_id)) {
            if (!strcmp(op, "<")) L->kind = TC_FOR_LT; else if (!strcmp(op, "<=")) L->kind = TC_FOR_LE; else if (!strcmp(op, "!=")) L->kind = TC_FOR_NE;
            else { snprintf(L->reason, sizeof L->reason, "condition operator %s not supported", op); return; }
        } else {
            iref = strip(cr); bound = cl;
            if (strcmp(jkind(iref), "DeclRefExpr")) { snprintf(L->reason, sizeof L->reason, "condition does not compare a counter variable"); return; }
            if (!strcmp(op, ">")) L->kind = TC_FOR_LT; else if (!strcmp(op, ">=")) L->kind = TC_FOR_LE; else if (!strcmp(op, "!=")) L->kind = TC_FOR_NE;
            else { snprintf(L->reason, sizeof L->reason, "condition operator %s not supported", op); return; }
        }
        L->iid = decl_id(iref); L->iname = decl_name(iref); L->it = parse_ctype(node_type(iref));
        if (!L->iid || !L->it.ok || L->it.is_volatile) { snprintf(L->reason, sizeof L->reason, "counter variable has an unsupported type"); return; }
        if (!strcmp(L->iid, L->xid)) { snprintf(L->reason, sizeof L->reason, "counter is the state variable"); return; }
        if (!pure_expr(bound) || mentions(bound, L->iid) || mentions(bound, L->xid)) { snprintf(L->reason, sizeof L->reason, "bound is not a loop-invariant pure expression"); return; }
        if (!node_range(bound, &L->n_range)) { snprintf(L->reason, sizeof L->reason, "bound has no source range"); return; }
        /* increment: ++i, i++, i += 1, i = i + 1 */
        {
            const jval *ic = strip(inc); const char *ik = jkind(ic); bool ok = false;
            if (!strcmp(ik, "UnaryOperator") && !strcmp(jstr(ic, "opcode"), "++") && is_declref_to(jinner_at(ic, 0), L->iid)) ok = true;
            else if (!strcmp(ik, "CompoundAssignOperator") && !strcmp(jstr(ic, "opcode"), "+=") && is_declref_to(jinner_at(ic, 0), L->iid)) { uint64_t v; ok = const_eval(jinner_at(ic, 1), &v) && v == 1; }
            else if (!strcmp(ik, "BinaryOperator") && !strcmp(jstr(ic, "opcode"), "=") && is_declref_to(jinner_at(ic, 0), L->iid)) {
                const jval *r = strip(jinner_at(ic, 1)); uint64_t v;
                if (!strcmp(jkind(r), "BinaryOperator") && !strcmp(jstr(r, "opcode"), "+")) ok = (is_declref_to(jinner_at(r, 0), L->iid) && const_eval(jinner_at(r, 1), &v) && v == 1) || (is_declref_to(jinner_at(r, 1), L->iid) && const_eval(jinner_at(r, 0), &v) && v == 1);
            }
            if (!ok) { snprintf(L->reason, sizeof L->reason, "increment is not ++%s", L->iname ? L->iname : "i"); return; }
        }
        /* init */
        L->i_declared_in_for = false; L->a_range.begin = -1; L->a_range.end = -1;
        if (init) {
            if (!strcmp(jkind(init), "DeclStmt")) {
                if (jinner_n(init) != 1) { snprintf(L->reason, sizeof L->reason, "for-init declares more than one variable"); return; }
                const jval *vd = jinner_at(init, 0);
                const char *vid = jstr(vd, "id");
                if (!vid || strcmp(vid, L->iid)) { snprintf(L->reason, sizeof L->reason, "for-init declares a variable other than the counter"); return; }
                const jval *iv = jinner_n(vd) ? jinner_at(vd, 0) : NULL;
                if (!iv || !pure_expr(iv) || mentions(iv, L->xid) || !node_range(iv, &L->a_range)) { snprintf(L->reason, sizeof L->reason, "counter initialiser is not a pure expression"); return; }
                L->i_declared_in_for = true;
            } else {
                const jval *ia = strip(init);
                if (strcmp(jkind(ia), "BinaryOperator") || strcmp(jstr(ia, "opcode"), "=") || !is_declref_to(jinner_at(ia, 0), L->iid)) { snprintf(L->reason, sizeof L->reason, "for-init is not '%s = A'", L->iname); return; }
                const jval *iv = jinner_at(ia, 1);
                if (!pure_expr(iv) || mentions(iv, L->xid) || mentions(iv, L->iid) || !node_range(iv, &L->a_range)) { snprintf(L->reason, sizeof L->reason, "counter initialiser is not a pure expression"); return; }
            }
        }
        if (mentions(L->body, L->iid)) { snprintf(L->reason, sizeof L->reason, "body uses the counter"); return; }
    } else {
        /* while (n--)  |  while (n-- > 0)  |  while (n-- != 0) */
        const jval *c = strip(cond); const jval *dec = c; L->while_signed_gt0 = false;
        if (!strcmp(jkind(c), "BinaryOperator")) {
            const char *op = jstr(c, "opcode"); uint64_t z;
            if ((!strcmp(op, ">") || !strcmp(op, "!=")) && const_eval(jinner_at(c, 1), &z) && z == 0) { dec = strip(jinner_at(c, 0)); L->while_signed_gt0 = !strcmp(op, ">"); }
            else { snprintf(L->reason, sizeof L->reason, "while condition is not n-- / n-- > 0"); return; }
        }
        if (strcmp(jkind(dec), "UnaryOperator") || strcmp(jstr(dec, "opcode"), "--") || !jbool(dec, "isPostfix")) { snprintf(L->reason, sizeof L->reason, "while condition is not n--"); return; }
        const jval *iref = strip(jinner_at(dec, 0));
        if (strcmp(jkind(iref), "DeclRefExpr")) { snprintf(L->reason, sizeof L->reason, "n-- on a non-variable"); return; }
        L->kind = TC_WHILE_DEC; L->iid = decl_id(iref); L->iname = decl_name(iref); L->it = parse_ctype(node_type(iref));
        if (!L->iid || !L->it.ok || L->it.is_volatile) { snprintf(L->reason, sizeof L->reason, "counter variable has an unsupported type"); return; }
        if (!strcmp(L->iid, L->xid)) { snprintf(L->reason, sizeof L->reason, "counter is the state variable"); return; }
        if (L->it.is_signed && !L->while_signed_gt0) { snprintf(L->reason, sizeof L->reason, "while (n--) with signed n: use n-- > 0"); return; }
        if (mentions(L->body, L->iid)) { snprintf(L->reason, sizeof L->reason, "body uses the counter"); return; }
        L->i_declared_in_for = false; L->a_range.begin = -1; L->a_range.end = -1;
    }

    /* body: assignments to x only */
    memset(&L->lc, 0, sizeof L->lc);
    L->lc.xid = L->xid; L->lc.w = L->xt.width; L->lc.ok = true; L->lc.next_reg = 1;
    const jval *body = L->body;
    if (!strcmp(jkind(body), "CompoundStmt")) {
        for (int i = 0; i < jinner_n(body) && L->lc.ok; ++i) lower_stmt(jinner_at(body, i), &L->lc);
    } else lower_stmt(body, &L->lc);
    if (!L->lc.ok) { snprintf(L->reason, sizeof L->reason, "body not lowerable: %s", L->lc.reason); return; }
    if (L->lc.n == 0) { snprintf(L->reason, sizeof L->reason, "empty body"); return; }

    /* the lifter proves affinity and derives the exact step matrix */
    mxl_loop ml = { (uint8_t)L->xt.width, 0, (uint16_t)L->lc.n, L->lc.code };
    mxl_report rep;
    mxl_status st = mxl_lift(&ml, &L->step, &rep);
    if (st != MXL_OK) { snprintf(L->reason, sizeof L->reason, "mxl_lift: %s (insn %d)", mxl_status_str(st), rep.insn); return; }
    /* belt and braces: the lifter's own naive interpreter must agree with the matrix on a few seeds */
    for (int t = 0; t < 8; ++t) {
        uint64_t seed = (0x9E3779B97F4A7C15ull * (uint64_t)(t + 1)) & mx_lift_mask(L->xt.width);
        if (mxl_step(&ml, seed) != mx_aff_apply(&L->step, seed)) { snprintf(L->reason, sizeof L->reason, "internal: step matrix disagrees with interpreter"); return; }
    }
    L->lifted = true;
}

/* collect loops in document order within main-file function bodies */
typedef struct { loop_info *v; int n, cap; } loop_vec;
static void collect(const jval *n, loop_vec *out, bool in_body) {
    if (!n || n->t != J_OBJ) return;
    const char *k = jkind(n);
    if (in_body && (!strcmp(k, "ForStmt") || !strcmp(k, "WhileStmt"))) {
        if (out->n == out->cap) { out->cap = out->cap ? out->cap * 2 : 16; out->v = realloc(out->v, (size_t)out->cap * sizeof *out->v); }
        loop_info *L = &out->v[out->n++]; memset(L, 0, sizeof *L); L->loop = n;
        analyse_loop(L);
        if (L->lifted) return;       /* do not descend into a lifted loop */
    }
    for (int i = 0; i < jinner_n(n); ++i) collect(jinner_at(n, i), out, in_body);
}

/* ------------------------------------------------------------------------ */
/* main                                                                      */
/* ------------------------------------------------------------------------ */
static int usage(void) { fprintf(stderr, "usage: mxlift_hook in.c -o out.c [--clang /abs/clang] [--mx-lift-include HDR] [--report] [--dry-run] [-- cflags]\n"); return 2; }

int main(int argc, char **argv) {
    const char *in = NULL, *out = NULL, *clang = "/usr/bin/clang", *hdr = "mx_lift.h";
    bool report = false, dry = false; char **extra = NULL; int nextra = 0;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-o") && i + 1 < argc) out = argv[++i];
        else if (!strcmp(argv[i], "--clang") && i + 1 < argc) clang = argv[++i];
        else if (!strcmp(argv[i], "--mx-lift-include") && i + 1 < argc) hdr = argv[++i];
        else if (!strcmp(argv[i], "--report")) report = true;
        else if (!strcmp(argv[i], "--dry-run")) dry = true;
        else if (!strcmp(argv[i], "--")) { extra = argv + i + 1; nextra = argc - i - 1; break; }
        else if (argv[i][0] == '-') return usage();
        else if (!in) in = argv[i]; else return usage();
    }
    if (!in || (!out && !dry)) return usage();
    if (clang[0] != '/') { fprintf(stderr, "mxlift_hook: --clang must be an absolute path (no PATH lookup)\n"); return 2; }

    size_t srclen; char *src = read_file(in, &srclen);
    if (!src) { fprintf(stderr, "mxlift_hook: cannot read %s\n", in); return 1; }
    char json_path[512]; { const char *td = getenv("TMPDIR"); if (!td || td[0] != '/') td = "/tmp"; snprintf(json_path, sizeof json_path, "%s/mxlift_ast_XXXXXX", td); int fd = mkstemp(json_path); if (fd < 0) return 1; close(fd); }
    int rc = run_clang(clang, in, extra, nextra, json_path);
    if (rc != 0) { fprintf(stderr, "mxlift_hook: clang failed (%d)\n", rc); remove(json_path); return 1; }
    size_t jlen; char *jtext = read_file(json_path, &jlen); remove(json_path);
    if (!jtext) { fprintf(stderr, "mxlift_hook: cannot read AST dump\n"); return 1; }
    char jerr[200]; jval *root = jparse(jtext, jlen, jerr, sizeof jerr);
    if (!root) { fprintf(stderr, "mxlift_hook: AST JSON parse error: %s\n", jerr); return 1; }

    /* main-file functions: track the current file statefully (clang prints `file` only when it changes) */
    loop_vec loops = {0};
    const char *cur_file = NULL; bool in_main = false;
    long long first_fn_offset = -1;
    for (int i = 0; i < jinner_n(root); ++i) {
        const jval *d = jinner_at(root, i);
        jval *loc = jget(d, "loc");
        const jval *l2 = loc && jget(loc, "expansionLoc") ? jget(loc, "expansionLoc") : loc;
        const char *f = l2 ? jstr(l2, "file") : NULL;
        if (f) { cur_file = f; in_main = !strcmp(f, in) || (strrchr(in, '/') && !strcmp(f, strrchr(in, '/') + 1)) || (strrchr(f, '/') && !strcmp(strrchr(f, '/') + 1, strrchr(in, '/') ? strrchr(in, '/') + 1 : in)); }
        if (!in_main || !cur_file) continue;
        if (strcmp(jkind(d), "FunctionDecl")) continue;
        srange fr; if (first_fn_offset < 0 && node_range(d, &fr)) first_fn_offset = fr.begin;
        for (int j = 0; j < jinner_n(d); ++j) { const jval *c = jinner_at(d, j); if (!strcmp(jkind(c), "CompoundStmt")) collect(c, &loops, true); }
    }

    /* report */
    int nlift = 0;
    for (int i = 0; i < loops.n; ++i) {
        loop_info *L = &loops.v[i];
        int line = 1; for (long long k = 0; k < L->range.begin && k < (long long)srclen; ++k) if (src[k] == '\n') line++;
        if (L->lifted) { nlift++; fprintf(stderr, "mxlift_hook: %s:%d: lifted loop over %s (%u-bit, %d lowered insns, add=0x%llx)\n", in, line, L->xname, L->xt.width, L->lc.n, (unsigned long long)L->step.add); }
        else if (report) fprintf(stderr, "mxlift_hook: %s:%d: left alone: %s\n", in, line, L->reason);
    }
    if (dry) { fprintf(stderr, "mxlift_hook: %d loop(s) seen, %d liftable\n", loops.n, nlift); jfree(root); free(jtext); free(src); free(loops.v); return 0; }

    /* rewrite: sort lifted loops by begin offset, verify non-overlap, splice */
    sbuf o = {0};
    if (nlift == 0) { sb_put(&o, src, srclen); }
    else {
        /* prelude position: after the last #include before the first function */
        long long ins = 0;
        for (long long k = 0; k < (long long)srclen && (first_fn_offset < 0 || k < first_fn_offset); ) {
            long long e = k; while (e < (long long)srclen && src[e] != '\n') ++e;
            long long s = k; while (s < e && (src[s] == ' ' || src[s] == '\t')) ++s;
            if (s < e && src[s] == '#') { long long t = s + 1; while (t < e && (src[t] == ' ' || src[t] == '\t')) ++t; if (t + 7 <= e && !strncmp(src + t, "include", 7)) ins = e + 1; }
            k = e + 1;
        }
        sb_put(&o, src, (size_t)ins);
        sb_printf(&o, "\n/* ---- mxlift_hook prelude: generated, do not edit ---- */\n#include \"%s\"\n", hdr);
        const char *helpers =
            "static inline uint64_t __mxl_trips_lt_u(uint64_t a, uint64_t n) { return n > a ? n - a : 0; }\n"
            "static inline uint64_t __mxl_trips_lt_s(int64_t a, int64_t n) { return n > a ? (uint64_t)n - (uint64_t)a : 0; }\n"
            "static inline uint64_t __mxl_trips_le_u(uint64_t a, uint64_t n) { return n >= a ? n - a + 1 : 0; }\n"
            "static inline uint64_t __mxl_trips_le_s(int64_t a, int64_t n) { return n >= a ? (uint64_t)n - (uint64_t)a + 1 : 0; }\n"
            "static inline uint64_t __mxl_trips_ne(uint64_t a, uint64_t n, unsigned w) { return (n - a) & mx_lift_mask(w); }\n";
        sb_put(&o, helpers, strlen(helpers));
        for (int i = 0, id = 0; i < loops.n; ++i) if (loops.v[i].lifted) {
            loop_info *L = &loops.v[i];
            sb_printf(&o, "static const mx_aff __mxl_aff_%d = { %u, 0x%llxull, {", id, L->step.w, (unsigned long long)L->step.add);
            for (unsigned k = 0; k < 64; ++k) sb_printf(&o, "%s0x%llxull", k ? "," : "", (unsigned long long)L->step.col[k]);
            sb_put(&o, "} };\n", 5);
            id++;
        }
        sb_put(&o, "/* ---- end mxlift_hook prelude ---- */\n", strlen("/* ---- end mxlift_hook prelude ---- */\n"));
        long long pos = ins; int id = 0;
        for (int i = 0; i < loops.n; ++i) if (loops.v[i].lifted) {
            loop_info *L = &loops.v[i];
            if (L->range.begin < pos) { fprintf(stderr, "mxlift_hook: internal: overlapping ranges\n"); return 1; }
            sb_put(&o, src + pos, (size_t)(L->range.begin - pos));
            char abuf[2048], nbuf[2048];
            const char *A = L->a_range.begin >= 0 ? get_source_text(src, srclen, L->a_range, abuf, sizeof abuf) : L->iname;
            const char *N = L->kind == TC_WHILE_DEC ? L->iname : get_source_text(src, srclen, L->n_range, nbuf, sizeof nbuf);
            if (!A || !N) { fprintf(stderr, "mxlift_hook: internal: cannot extract source text\n"); return 1; }
            const char *IT = L->it.is_signed ? (L->it.width == 64 ? "int64_t" : L->it.width == 32 ? "int32_t" : L->it.width == 16 ? "int16_t" : "int8_t")
                                              : (L->it.width == 64 ? "uint64_t" : L->it.width == 32 ? "uint32_t" : L->it.width == 16 ? "uint16_t" : "uint8_t");
            const char *XT = L->xt.width == 64 ? "uint64_t" : L->xt.width == 32 ? "uint32_t" : L->xt.width == 16 ? "uint16_t" : "uint8_t";
            sb_printf(&o, "{ /* mxlift_hook: GF(2) affine recurrence on %s lifted to one O(log n) jump */\n", L->xname);
            if (L->kind == TC_WHILE_DEC) {
                sb_printf(&o, "  %s __mxl_a = (%s); uint64_t __mxl_n = %s;\n", IT, A, L->it.is_signed ? "(__mxl_a > 0 ? (uint64_t)__mxl_a : 0)" : "(uint64_t)__mxl_a");
                sb_printf(&o, "  %s = (%s)mx_aff_jump(&__mxl_aff_%d, (uint64_t)%s, __mxl_n);\n", L->xname, XT, id, L->xname);
                sb_printf(&o, "  %s = (%s)((uint64_t)__mxl_a - __mxl_n - 1u); }", L->iname, IT);
            } else {
                const char *tf = L->kind == TC_FOR_LT ? (L->it.is_signed ? "__mxl_trips_lt_s" : "__mxl_trips_lt_u") : L->kind == TC_FOR_LE ? (L->it.is_signed ? "__mxl_trips_le_s" : "__mxl_trips_le_u") : "__mxl_trips_ne";
                sb_printf(&o, "  %s __mxl_a = (%s); %s __mxl_b = (%s);\n", IT, A, IT, N);
                if (L->kind == TC_FOR_NE) sb_printf(&o, "  uint64_t __mxl_n = %s((uint64_t)__mxl_a, (uint64_t)__mxl_b, %u);\n", tf, L->it.width);
                else sb_printf(&o, "  uint64_t __mxl_n = %s(__mxl_a, __mxl_b);\n", tf);
                sb_printf(&o, "  %s = (%s)mx_aff_jump(&__mxl_aff_%d, (uint64_t)%s, __mxl_n);\n", L->xname, XT, id, L->xname);
                if (!L->i_declared_in_for) sb_printf(&o, "  %s = (%s)((uint64_t)__mxl_a + __mxl_n);\n", L->iname, IT);
                sb_put(&o, "}", 1);
            }
            pos = L->range.end; id++;
        }
        sb_put(&o, src + pos, srclen - (size_t)pos);
    }
    FILE *f = fopen(out, "wb"); if (!f) { fprintf(stderr, "mxlift_hook: cannot write %s\n", out); return 1; }
    fwrite(o.s, 1, o.n, f); fclose(f);
    fprintf(stderr, "mxlift_hook: %d loop(s) seen, %d lifted -> %s\n", loops.n, nlift, out);
    jfree(root); free(jtext); free(src); free(o.s); free(loops.v);
    return 0;
}
