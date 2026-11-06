/* Genuine Doom's current default zone = 6 MiB, documented by issue #239.
 * Test the existing actual arena allocator at 8 MiB without aliasing libc.
 * This proves capacity to allocate the zone, not runtime game startup.
 */
#include "arena.h"
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

enum { MIB = 1024 * 1024 };
_Static_assert(DOOM_ARENA_BYTES == 8u * MIB, "expected exactly 8 MiB bounded arena");

int main(void) {
    doom_arena_reset();
    void *zone = doom_alloc(6u * MIB);
    assert(zone != NULL);
    assert((uintptr_t)zone % _Alignof(max_align_t) == 0);
    assert(doom_arena_used() == 6u * MIB);
    memset(zone, 0x5a, 6u * MIB);
    assert(((unsigned char *)zone)[0] == 0x5a);
    assert(((unsigned char *)zone)[6u * MIB - 1] == 0x5a);
    void *scratch = doom_alloc(1024u * 1024u);
    assert(scratch != NULL && scratch != zone);
    assert(doom_arena_used() == 7u * MIB);
    assert(doom_alloc(2u * MIB) == NULL);
    assert(doom_arena_used() == 7u * MIB);
    doom_free(scratch);
    doom_free(zone);
    assert(doom_arena_used() == 0);
    zone = doom_alloc(6u * MIB);
    assert(zone != NULL);
    doom_free(zone);
    puts("PASS: actual 8 MiB arena allocates genuine 6 MiB Doom zone and 1 MiB scratch; bounds and reclaim verified");
    return 0;
}
