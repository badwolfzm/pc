/* Freestanding <stdlib.h>: a bounded arena allocator and abort, nothing else.
 * The names are macros onto mx_* so the objects never bind to a hosted libc. */
#ifndef MX_FS_STDLIB_H
#define MX_FS_STDLIB_H
#include <stddef.h>
void *mx_malloc(size_t n);
void *mx_calloc(size_t n, size_t size);
void  mx_free(void *p);
_Noreturn void mx_abort(void);   /* provided by the environment: SYS_EXIT in Warden */
#define malloc mx_malloc
#define calloc mx_calloc
#define free   mx_free
#define abort  mx_abort
#endif
