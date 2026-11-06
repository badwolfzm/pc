/* mxlibc.c - freestanding runtime for the render service.
 *
 * Allocator: first-fit over one arena. Each block is a 16-byte header
 * {size, used} followed by `size` payload bytes (a multiple of 16), and the
 * next block follows physically. free() only marks the block; malloc()
 * merges runs of free blocks while it scans, so fragmentation is undone the
 * next time memory is asked for. Every size computation is overflow-checked;
 * a pointer that is not the start of a used block in the arena aborts.
 * Allocation is O(blocks), which is fine for surfaces (tens of blocks).
 */
#include "mxlibc.h"
#include <string.h>   /* freestanding/include/string.h in the image; libc's in host tests */

#ifndef MXLIBC_NO_STRING
/* Plain byte loops: the compiler is not allowed to turn these into calls to
 * themselves (-fno-builtin / -fno-tree-loop-distribute-patterns in the Makefile). */
void *memcpy(void *restrict d, const void *restrict s, size_t n) {
#if defined(__x86_64__)
    /* rep movsb: fast on every CPU with ERMSB (the i3-14100 has it). */
    void *ret = d;
    __asm__ volatile("rep movsb" : "+D"(d), "+S"(s), "+c"(n) : : "memory");
    return ret;
#else
    unsigned char *dp = d; const unsigned char *sp = s;
    while (n--) *dp++ = *sp++;
    return d;
#endif
}
void *memmove(void *d, const void *s, size_t n) {
    unsigned char *dp = d; const unsigned char *sp = s;
    if (dp == sp || n == 0) return d;
    if (dp < sp) { while (n--) *dp++ = *sp++; }
    else { dp += n; sp += n; while (n--) *--dp = *--sp; }
    return d;
}
void *memset(void *d, int c, size_t n) {
#if defined(__x86_64__)
    void *ret = d;
    __asm__ volatile("rep stosb" : "+D"(d), "+c"(n) : "a"(c) : "memory");
    return ret;
#else
    unsigned char *dp = d;
    while (n--) *dp++ = (unsigned char)c;
    return d;
#endif
}
int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *x = a, *y = b;
    for (; n; --n, ++x, ++y) if (*x != *y) return *x < *y ? -1 : 1;
    return 0;
}
#endif

typedef struct { uint64_t size; uint64_t used; } hdr;
_Static_assert(sizeof(hdr) == 16, "header is 16 bytes");

static unsigned char *g_base, *g_end;
static size_t g_in_use;

static hdr *next_of(hdr *b) { return (hdr *)((unsigned char *)b + sizeof(hdr) + b->size); }

int mx_heap_init(void *base, size_t len) {
    uintptr_t b = ((uintptr_t)base + 15u) & ~(uintptr_t)15u;
    uintptr_t e = ((uintptr_t)base + len) & ~(uintptr_t)15u;
    if (!base || e <= b || e - b < 2 * sizeof(hdr)) return -1;
    g_base = (unsigned char *)b; g_end = (unsigned char *)e; g_in_use = 0;
    hdr *h = (hdr *)g_base; h->size = (uint64_t)(e - b - sizeof(hdr)); h->used = 0;
    return 0;
}

void *mx_malloc(size_t n) {
    if (!g_base || n == 0 || n > (size_t)(g_end - g_base)) return NULL;
    uint64_t want = ((uint64_t)n + 15u) & ~(uint64_t)15u;
    for (hdr *b = (hdr *)g_base; (unsigned char *)b < g_end; b = next_of(b)) {
        if (b->used) continue;
        /* Merge the run of free blocks that follows. */
        for (hdr *nx = next_of(b); (unsigned char *)nx < g_end && !nx->used; nx = next_of(b))
            b->size += sizeof(hdr) + nx->size;
        if (b->size < want) continue;
        if (b->size >= want + sizeof(hdr) + 16u) {          /* split */
            hdr *rest = (hdr *)((unsigned char *)b + sizeof(hdr) + want);
            rest->size = b->size - want - sizeof(hdr); rest->used = 0;
            b->size = want;
        }
        b->used = 1; g_in_use += b->size;
        return (unsigned char *)b + sizeof(hdr);
    }
    return NULL;
}

void *mx_calloc(size_t n, size_t size) {
    if (size && n > (size_t)-1 / size) return NULL;
    void *p = mx_malloc(n * size);
    if (p) memset(p, 0, n * size);
    return p;
}

void mx_free(void *p) {
    if (!p) return;
    unsigned char *c = p;
    if (c < g_base + sizeof(hdr) || c >= g_end || ((uintptr_t)c & 15u)) mx_abort();
    /* Must be the payload of a used block: walk to it (also catches double free). */
    for (hdr *b = (hdr *)g_base; (unsigned char *)b < g_end; b = next_of(b)) {
        unsigned char *pl = (unsigned char *)b + sizeof(hdr);
        if (pl == c) { if (!b->used) mx_abort(); b->used = 0; g_in_use -= b->size; return; }
        if (pl > c) break;
    }
    mx_abort();
}

size_t mx_heap_in_use(void) { return g_in_use; }

size_t mx_heap_free_bytes(void) {
    size_t best = 0;
    for (hdr *b = (hdr *)g_base; g_base && (unsigned char *)b < g_end; b = next_of(b)) {
        if (b->used) continue;
        uint64_t run = b->size;
        for (hdr *nx = next_of(b); (unsigned char *)nx < g_end && !nx->used; nx = next_of(nx)) run += sizeof(hdr) + nx->size;
        if (run > best) best = (size_t)run;
    }
    return best;
}
