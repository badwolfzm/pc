/* Minimal JSON DOM parser for clang -ast-dump=json output. C99, arena based. */
#ifndef MXL_JSON_H
#define MXL_JSON_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef enum { J_NULL, J_BOOL, J_NUM, J_STR, J_ARR, J_OBJ } jtype;
typedef struct jval jval;
struct jval {
    jtype t;
    const char *s;      /* string value (NUL-terminated, unescaped) or number text */
    double num;
    bool b;
    jval **items;       /* array items / object values */
    const char **keys;  /* object keys */
    int n;
};

typedef struct { const char *p, *end; int line; char err[200]; } jparser;

static jval *jnew(jtype t) { jval *v = calloc(1, sizeof *v); v->t = t; return v; }
static void jskip(jparser *ps) { while (ps->p < ps->end && (*ps->p == ' ' || *ps->p == '\t' || *ps->p == '\n' || *ps->p == '\r')) { if (*ps->p == '\n') ps->line++; ps->p++; } }
static jval *jparse_value(jparser *ps);

static char *jparse_string(jparser *ps) {
    if (ps->p >= ps->end || *ps->p != '"') { snprintf(ps->err, sizeof ps->err, "line %d: expected string", ps->line); return NULL; }
    ps->p++;
    size_t cap = 32, n = 0; char *out = malloc(cap);
    while (ps->p < ps->end && *ps->p != '"') {
        char c = *ps->p++;
        if (n + 8 >= cap) { cap *= 2; out = realloc(out, cap); }
        if (c == '\\') {
            if (ps->p >= ps->end) break;
            char e = *ps->p++;
            switch (e) {
                case 'n': out[n++] = '\n'; break; case 't': out[n++] = '\t'; break; case 'r': out[n++] = '\r'; break;
                case 'b': out[n++] = '\b'; break; case 'f': out[n++] = '\f'; break;
                case 'u': {
                    unsigned cp = 0; for (int k = 0; k < 4 && ps->p < ps->end; ++k) { char h = *ps->p++; cp = cp * 16 + (unsigned)(h <= '9' ? h - '0' : (h | 32) - 'a' + 10); }
                    if (cp < 0x80) out[n++] = (char)cp; else if (cp < 0x800) { out[n++] = (char)(0xC0 | (cp >> 6)); out[n++] = (char)(0x80 | (cp & 63)); }
                    else { out[n++] = (char)(0xE0 | (cp >> 12)); out[n++] = (char)(0x80 | ((cp >> 6) & 63)); out[n++] = (char)(0x80 | (cp & 63)); }
                    break;
                }
                default: out[n++] = e;
            }
        } else out[n++] = c;
    }
    if (ps->p >= ps->end) { free(out); snprintf(ps->err, sizeof ps->err, "unterminated string"); return NULL; }
    ps->p++; out[n] = 0; return out;
}
static jval *jparse_value(jparser *ps) {
    jskip(ps);
    if (ps->p >= ps->end) { snprintf(ps->err, sizeof ps->err, "unexpected end"); return NULL; }
    char c = *ps->p;
    if (c == '{') {
        ps->p++; jval *o = jnew(J_OBJ); int cap = 0;
        jskip(ps);
        if (ps->p < ps->end && *ps->p == '}') { ps->p++; return o; }
        for (;;) {
            jskip(ps);
            char *k = jparse_string(ps); if (!k) return NULL;
            jskip(ps); if (ps->p >= ps->end || *ps->p != ':') { snprintf(ps->err, sizeof ps->err, "line %d: expected ':'", ps->line); return NULL; } ps->p++;
            jval *v = jparse_value(ps); if (!v) return NULL;
            if (o->n == cap) { cap = cap ? cap * 2 : 8; o->items = realloc(o->items, (size_t)cap * sizeof *o->items); o->keys = realloc(o->keys, (size_t)cap * sizeof *o->keys); }
            o->keys[o->n] = k; o->items[o->n] = v; o->n++;
            jskip(ps);
            if (ps->p < ps->end && *ps->p == ',') { ps->p++; continue; }
            if (ps->p < ps->end && *ps->p == '}') { ps->p++; return o; }
            snprintf(ps->err, sizeof ps->err, "line %d: expected ',' or '}'", ps->line); return NULL;
        }
    }
    if (c == '[') {
        ps->p++; jval *a = jnew(J_ARR); int cap = 0;
        jskip(ps);
        if (ps->p < ps->end && *ps->p == ']') { ps->p++; return a; }
        for (;;) {
            jval *v = jparse_value(ps); if (!v) return NULL;
            if (a->n == cap) { cap = cap ? cap * 2 : 8; a->items = realloc(a->items, (size_t)cap * sizeof *a->items); }
            a->items[a->n++] = v;
            jskip(ps);
            if (ps->p < ps->end && *ps->p == ',') { ps->p++; continue; }
            if (ps->p < ps->end && *ps->p == ']') { ps->p++; return a; }
            snprintf(ps->err, sizeof ps->err, "line %d: expected ',' or ']'", ps->line); return NULL;
        }
    }
    if (c == '"') { char *s = jparse_string(ps); if (!s) return NULL; jval *v = jnew(J_STR); v->s = s; return v; }
    if (!strncmp(ps->p, "true", 4)) { ps->p += 4; jval *v = jnew(J_BOOL); v->b = true; return v; }
    if (!strncmp(ps->p, "false", 5)) { ps->p += 5; jval *v = jnew(J_BOOL); return v; }
    if (!strncmp(ps->p, "null", 4)) { ps->p += 4; return jnew(J_NULL); }
    if (c == '-' || (c >= '0' && c <= '9')) {
        const char *s = ps->p; while (ps->p < ps->end && (*ps->p == '-' || *ps->p == '+' || *ps->p == '.' || *ps->p == 'e' || *ps->p == 'E' || (*ps->p >= '0' && *ps->p <= '9'))) ps->p++;
        jval *v = jnew(J_NUM); size_t n = (size_t)(ps->p - s); char *t = malloc(n + 1); memcpy(t, s, n); t[n] = 0; v->s = t; v->num = strtod(t, NULL); return v;
    }
    snprintf(ps->err, sizeof ps->err, "line %d: unexpected '%c'", ps->line, c); return NULL;
}
static jval *jparse(const char *text, size_t len, char *err, size_t errlen) {
    jparser ps = { text, text + len, 1, {0} };
    jval *v = jparse_value(&ps);
    if (!v && err) snprintf(err, errlen, "%s", ps.err);
    return v;
}
static void jfree(jval *v) {
    if (!v) return;
    if (v->t == J_OBJ || v->t == J_ARR) { for (int i = 0; i < v->n; ++i) { jfree(v->items[i]); if (v->keys) free((void *)v->keys[i]); } free(v->items); free((void *)v->keys); }
    if (v->t == J_STR || v->t == J_NUM) free((void *)v->s);
    free(v);
}
static jval *jget(const jval *o, const char *key) {
    if (!o || o->t != J_OBJ) return NULL;
    for (int i = 0; i < o->n; ++i) if (strcmp(o->keys[i], key) == 0) return o->items[i];
    return NULL;
}
static const char *jstr(const jval *o, const char *key) { jval *v = jget(o, key); return v && v->t == J_STR ? v->s : NULL; }
static bool jhas_num(const jval *o, const char *key, long long *out) { jval *v = jget(o, key); if (!v || v->t != J_NUM) return false; *out = strtoll(v->s, NULL, 10); return true; }
static bool jbool(const jval *o, const char *key) { jval *v = jget(o, key); return v && v->t == J_BOOL && v->b; }
static jval *jinner(const jval *o) { jval *v = jget(o, "inner"); return v && v->t == J_ARR ? v : NULL; }
static int jinner_n(const jval *o) { jval *a = jinner(o); return a ? a->n : 0; }
static jval *jinner_at(const jval *o, int i) { jval *a = jinner(o); return a && i < a->n ? a->items[i] : NULL; }
static const char *jkind(const jval *o) { const char *k = jstr(o, "kind"); return k ? k : ""; }
#endif
