#include "arena.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdalign.h>

int main(void) {
    doom_arena_reset();
    assert(doom_arena_used() == 0);
    assert(!doom_alloc(0));
    assert(!doom_alloc(SIZE_MAX));
    assert(!doom_calloc(SIZE_MAX, 3));
    unsigned char *a = doom_alloc(123);
    assert(a && (uintptr_t)a % alignof(max_align_t) == 0);
    memset(a, 0x5a, 123);
    assert(doom_arena_used() == 123);
    unsigned char *b = doom_calloc(100, 10);
    assert(b);
    for (int i = 0; i < 1000; ++i) assert(!b[i]);
    b = doom_realloc(b, 2000);
    assert(b);
    for (int i = 0; i < 1000; ++i) assert(!b[i]);
    for (int i = 0; i < 123; ++i) assert(a[i] == 0x5a);
    b = doom_realloc(b, 16);
    assert(b);
    assert(doom_arena_used() == 139);
    doom_free(b);
    doom_free(a);
    assert(doom_arena_used() == 0);
    void *large = doom_alloc(DOOM_ARENA_BYTES - 4096);
    assert(large);
    assert(!doom_alloc(8192));
    void *failed = doom_realloc(large, DOOM_ARENA_BYTES + 1u);
    assert(!failed && doom_arena_used() == DOOM_ARENA_BYTES - 4096);
    doom_free(large);
    doom_arena_reset();
    void *many[4096];
    for (int i = 0; i < 4096; ++i) { many[i] = doom_alloc(64); assert(many[i]); }
    for (int i = 1; i < 4096; i += 2) doom_free(many[i]);
    for (int i = 0; i < 4096; i += 2) doom_free(many[i]);
    assert(doom_arena_used() == 0);
    assert(doom_alloc(DOOM_ARENA_BYTES - 4096));
    puts("PASS allocator alignment, exhaustion, calloc, realloc, reuse, coalescing");
}
