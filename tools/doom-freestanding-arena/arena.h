#ifndef WARDEN_DOOM_ARENA_H
#define WARDEN_DOOM_ARENA_H
#include <stddef.h>
#define DOOM_ARENA_BYTES (4u * 1024u * 1024u)
void doom_arena_reset(void);
void *doom_alloc(size_t n);
void doom_free(void *p);
void *doom_calloc(size_t count, size_t size);
void *doom_realloc(void *p, size_t n);
size_t doom_arena_used(void);
#endif
