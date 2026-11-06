#ifndef WARDEN_DOOM_ARENA_H
#define WARDEN_DOOM_ARENA_H
#include <stddef.h>
/* Build-time fixed arena capacity. Measured Doom runtime usage must determine
 * the real guest setting; a smaller arena does not prove a native boot.
 */
#ifndef DOOM_ARENA_BYTES
#define DOOM_ARENA_BYTES (4u * 1024u * 1024u)
#endif
#if DOOM_ARENA_BYTES < 524288u || DOOM_ARENA_BYTES > (16u * 1024u * 1024u)
#error "DOOM_ARENA_BYTES must be between 512 KiB and 16 MiB"
#endif
void doom_arena_reset(void);
void *doom_alloc(size_t n);
void doom_free(void *p);
void *doom_calloc(size_t count, size_t size);
void *doom_realloc(void *p, size_t n);
size_t doom_arena_used(void);
#endif
