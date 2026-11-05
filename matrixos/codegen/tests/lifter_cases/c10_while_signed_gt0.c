/* while (n-- > 0) with a signed counter, n read after */
#include "common.h"
uint32_t step(uint32_t x, int n, int *nafter) {
    while (n-- > 0) x = (x >> 1) ^ (0x80200003u & (0u - (x & 1u)));
    *nafter = n;
    return x;
}
int main(void) { int ns[] = { -3, -1, 0, 1, 2, 10, 1000000 }; for (unsigned s = 0; s < NSEEDS; ++s) for (unsigned k = 0; k < 7; ++k) { int na; uint32_t r = step((uint32_t)SEEDS[s], ns[k], &na); printf("%08" PRIx32 " %d\n", r, na); } return 0; }
