/* rotate idiom through shifts and OR of disjoint fields, with xor constant */
#include "common.h"
uint32_t step(uint32_t x, uint64_t n) {
    for (uint64_t i = 0; i < n; ++i) x = ((x << 7) | (x >> 25)) ^ 0x9E3779B9u;
    return x;
}
int main(void) { for (unsigned s = 0; s < NSEEDS; ++s) for (unsigned c = 0; c < NCOUNTS; ++c) printf("%08" PRIx32 "\n", step((uint32_t)SEEDS[s], COUNTS[c])); return 0; }
