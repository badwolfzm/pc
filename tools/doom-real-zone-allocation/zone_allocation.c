/* Execute the existing Warden Doom arena at two configured capacities.
 * This is a host-side allocator integration test, NOT a native game boot.
 */
#include "arena.h"
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define MIB ((size_t)1024 * 1024)
#define ZONE_BYTES (6 * MIB)
#ifndef ZONE_EXPECT_SUCCESS
#error "ZONE_EXPECT_SUCCESS must be defined"
#endif
#if ZONE_EXPECT_SUCCESS != 0 && ZONE_EXPECT_SUCCESS != 1
#error "ZONE_EXPECT_SUCCESS must be 0 or 1"
#endif

int main(void) {
    doom_arena_reset();
    assert(doom_arena_used() == 0);
    unsigned char *lead = doom_alloc(32768);
    assert(lead != NULL);
    memset(lead, 0x3c, 32768);
    /* The real i_system.c I_ZoneBase default is malloc(6*1024*1024).
     * heap_alias.c delegates malloc to this exact doom_alloc function.
     */
    unsigned char *zone = doom_alloc(ZONE_BYTES);
#if ZONE_EXPECT_SUCCESS
    assert(zone != NULL);
    zone[0] = 0xa5;
    zone[ZONE_BYTES/2] = 0x5a;
    zone[ZONE_BYTES-1] = 0xc3;
    assert(zone[0] == 0xa5 && zone[ZONE_BYTES/2] == 0x5a
           && zone[ZONE_BYTES-1] == 0xc3);
    assert(doom_arena_used() == ZONE_BYTES + 32768);
    assert(doom_alloc(DOOM_ARENA_BYTES) == NULL);
    assert(doom_arena_used() == ZONE_BYTES + 32768);
    doom_free(zone);
    assert(doom_arena_used() == 32768);
    zone = doom_alloc(ZONE_BYTES);
    assert(zone != NULL);
    doom_free(zone);
#else
    assert(zone == NULL);
    assert(doom_arena_used() == 32768);
#endif
    for (size_t i = 0; i < 32768; ++i) assert(lead[i] == 0x3c);
    doom_free(lead);
    assert(doom_arena_used() == 0);
    doom_arena_reset();
    assert(doom_arena_used() == 0);
    printf("PASS real arena: capacity=%zu, 6MiB_zone=%s, accounting and cleanup\n",
           (size_t)DOOM_ARENA_BYTES, ZONE_EXPECT_SUCCESS ? "accepted" : "rejected");
    return 0;
}
