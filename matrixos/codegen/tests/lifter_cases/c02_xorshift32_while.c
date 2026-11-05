/* xorshift32, while (n--), n read after the loop */
#include "common.h"
uint32_t step(uint32_t x, unsigned n, unsigned *nafter) {
    while (n--) { x ^= x << 13; x ^= x >> 17; x ^= x << 5; }
    *nafter = n;
    return x;
}
int main(void) { for (unsigned s = 0; s < NSEEDS; ++s) for (unsigned c = 0; c < NCOUNTS; ++c) { unsigned na; uint32_t r = step((uint32_t)SEEDS[s], (unsigned)COUNTS[c], &na); printf("%08" PRIx32 " %u\n", r, na); } return 0; }
