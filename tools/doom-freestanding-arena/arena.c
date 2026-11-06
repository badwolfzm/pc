#include "arena.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdalign.h>

/* Deterministic single-thread arena. Not hardened against invalid pointers. */
#define ALIGNMENT ((size_t)alignof(max_align_t))
#define ALIGN_UP(x) (((x) + ALIGNMENT - 1u) & ~(ALIGNMENT - 1u))
typedef struct Block {
    size_t capacity;
    struct Block *prev, *next;
    size_t used;
} Block;
#define HEADER ALIGN_UP(sizeof(Block))
static union { max_align_t align; unsigned char bytes[DOOM_ARENA_BYTES]; } arena;
static Block *head;
static size_t live_bytes;
static int initialized;

void doom_arena_reset(void) {
    head = (Block *)(void *)arena.bytes;
    head->capacity = DOOM_ARENA_BYTES - HEADER;
    head->next = head->prev = NULL;
    head->used = 0;
    live_bytes = 0;
    initialized = 1;
}
static void ensure_init(void) { if (!initialized) doom_arena_reset(); }
static void split(Block *b, size_t need) {
    if (b->capacity - need < HEADER + ALIGNMENT) return;
    Block *rest = (Block *)(void *)((unsigned char *)b + HEADER + need);
    rest->capacity = b->capacity - need - HEADER;
    rest->used = 0;
    rest->prev = b;
    rest->next = b->next;
    if (rest->next) rest->next->prev = rest;
    b->next = rest;
    b->capacity = need;
}
void *doom_alloc(size_t n) {
    ensure_init();
    if (!n || n > DOOM_ARENA_BYTES || n > SIZE_MAX - (ALIGNMENT - 1u)) return NULL;
    size_t need = ALIGN_UP(n);
    for (Block *b = head; b; b = b->next) {
        if (!b->used && b->capacity >= need) {
            split(b, need);
            b->used = n;
            live_bytes += n;
            return (unsigned char *)b + HEADER;
        }
    }
    return NULL;
}
static void merge_next(Block *b) {
    Block *next = b->next;
    if (!next || next->used) return;
    b->capacity += HEADER + next->capacity;
    b->next = next->next;
    if (b->next) b->next->prev = b;
}
void doom_free(void *p) {
    if (!p) return;
    ensure_init();
    /* Only valid live pointers returned by this allocator are accepted. */
    for (Block *b = head; b; b = b->next) {
        if ((unsigned char *)b + HEADER == (unsigned char *)p) {
            if (!b->used) return;
            live_bytes -= b->used;
            b->used = 0;
            merge_next(b);
            if (b->prev && !b->prev->used) merge_next(b->prev);
            return;
        }
    }
}
void *doom_calloc(size_t count, size_t size) {
    if (size && count > SIZE_MAX / size) return NULL;
    size_t bytes = count * size;
    void *p = doom_alloc(bytes);
    if (p) memset(p, 0, bytes);
    return p;
}
void *doom_realloc(void *p, size_t n) {
    if (!p) return doom_alloc(n);
    if (!n) { doom_free(p); return NULL; }
    ensure_init();
    for (Block *b = head; b; b = b->next) {
        if ((unsigned char *)b + HEADER == (unsigned char *)p) {
            if (!b->used) return NULL;
            if (n <= b->capacity) {
                live_bytes = live_bytes - b->used + n;
                b->used = n;
                return p;
            }
            void *q = doom_alloc(n);
            if (!q) return NULL;
            memcpy(q, p, b->used);
            doom_free(p);
            return q;
        }
    }
    return NULL;
}
size_t doom_arena_used(void) { ensure_init(); return live_bytes; }
