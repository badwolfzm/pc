/* Unit test for the freestanding allocator and string routines. */
#include "mxlibc.h"
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int pass, fail;
#define CHECK(e) do { if (e) pass++; else { fail++; fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #e); } } while (0)
static jmp_buf jb; static int aborted;
_Noreturn void mx_abort(void) { aborted = 1; longjmp(jb, 1); }
#define EXPECT_ABORT(stmt) do { aborted = 0; if (!setjmp(jb)) { stmt; } CHECK(aborted); } while (0)

static unsigned rs = 12345;
static unsigned rnd(void) { rs = rs * 1103515245u + 12345u; return rs >> 8; }

int main(void) {
    static unsigned char arena[4 << 20];
    CHECK(mx_heap_init(NULL, 100) == -1);
    CHECK(mx_heap_init(arena, 16) == -1);
    CHECK(mx_heap_init(arena + 3, sizeof arena - 3) == 0);
    size_t full = mx_heap_free_bytes();
    CHECK(full > (4u << 20) - 64);
    CHECK(mx_malloc(0) == NULL);
    CHECK(mx_malloc((size_t)-1) == NULL);
    CHECK(mx_calloc((size_t)1 << 40, (size_t)1 << 40) == NULL);   /* n*size overflows */

    /* Random alloc/free with patterns: no two live blocks overlap, payloads keep their bytes. */
    enum { N = 400 };
    unsigned char *p[N] = {0}; size_t sz[N] = {0};
    int ok = 1;
    for (int round = 0; round < 20000; ++round) {
        int i = (int)(rnd() % N);
        if (p[i]) {
            for (size_t k = 0; k < sz[i]; ++k) if (p[i][k] != (unsigned char)(i + k)) ok = 0;
            mx_free(p[i]); p[i] = NULL;
        } else {
            sz[i] = 1 + rnd() % 20000;
            p[i] = mx_malloc(sz[i]);
            if (p[i]) { if (((uintptr_t)p[i] & 15u) != 0) ok = 0; for (size_t k = 0; k < sz[i]; ++k) p[i][k] = (unsigned char)(i + k); }
        }
    }
    CHECK(ok);
    for (int i = 0; i < N; ++i) mx_free(p[i]);
    CHECK(mx_heap_in_use() == 0);
    CHECK(mx_heap_free_bytes() == full);             /* fully coalesced again */
    unsigned char *big = mx_malloc(full);            /* the whole arena in one block */
    CHECK(big != NULL);
    CHECK(mx_malloc(16) == NULL);                    /* nothing left */
    mx_free(big);

    unsigned char *c = mx_calloc(1000, 4);
    int zero = 1; for (int k = 0; k < 4000; ++k) if (c[k]) zero = 0;
    CHECK(zero);
    EXPECT_ABORT(mx_free(c + 16));                   /* not the start of a block */
    EXPECT_ABORT(mx_free(arena));                    /* before the arena payloads */
    mx_free(c);
    EXPECT_ABORT(mx_free(c));                        /* double free */
    int stack_var; EXPECT_ABORT(mx_free(&stack_var));

    /* memmove both directions. */
    char s[32] = "abcdefghijklmnop";
    memmove(s + 2, s, 10); CHECK(memcmp(s, "ababcdefghijmnop", 16) == 0);
    memmove(s, s + 2, 10); CHECK(memcmp(s, "abcdefghijij", 12) == 0);
    printf("test_mxlibc: %d passed, %d failed\n", pass, fail);
    return fail != 0;
}
