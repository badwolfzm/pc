/* Link-only Doom allocator adapter: malloc/calloc/realloc/free -> bounded
 * prototype arena from PR #113. This is NOT an accepted general C runtime.
 * No fake syscall or host filesystem implementation is permitted here.
 */
#include <stddef.h>
#include "arena.h"
void *malloc(size_t n) { return doom_alloc(n); }
void free(void *p) { doom_free(p); }
void *calloc(size_t n, size_t size) { return doom_calloc(n, size); }
void *realloc(void *p, size_t n) { return doom_realloc(p, n); }
