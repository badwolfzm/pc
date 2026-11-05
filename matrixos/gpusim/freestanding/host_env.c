/* Host stand-in for the Warden environment of the freestanding render
 * service: one arena handed to mxlibc before main(), abort on mx_abort.
 * In Warden the arena is frames paid from the game's untyped budget. */
#include "mxlibc.h"
#include <stdio.h>
#include <stdlib.h>
#ifndef HOST_ARENA_MB
#define HOST_ARENA_MB 512
#endif
__attribute__((constructor)) static void host_arena(void) {
    size_t len = (size_t)HOST_ARENA_MB << 20;
    void *p = malloc(len);   /* hosted libc, only here */
    if (!p || mx_heap_init(p, len) != 0) { fprintf(stderr, "host_env: no arena\n"); exit(2); }
}
_Noreturn void mx_abort(void) { fprintf(stderr, "mx_abort\n"); abort(); }
