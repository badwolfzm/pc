/* mxlibc.h - the whole C runtime the freestanding render service needs.
 * The environment gives it one region of memory at start-up (in Warden: frames
 * paid from the game's untyped budget, mapped RW+NX) and provides mx_abort. */
#ifndef MXLIBC_H
#define MXLIBC_H
#include <stddef.h>
#include <stdint.h>
/* Hand the allocator its arena. base is rounded up to 16; returns 0, or -1 if too small. */
int    mx_heap_init(void *base, size_t len);
size_t mx_heap_free_bytes(void);   /* largest single allocation currently possible */
size_t mx_heap_in_use(void);       /* payload bytes allocated */
void  *mx_malloc(size_t n);
void  *mx_calloc(size_t n, size_t size);
void   mx_free(void *p);
_Noreturn void mx_abort(void);
#endif
