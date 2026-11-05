/* v1 recurrence: galois LFSR with affine constant, for (i < n) */
#include "common.h"
#define POLY 0xEDB88320u
#define ADD  0xA5A5A5A5u
uint32_t step(uint32_t x, uint64_t n) {
    for (uint64_t i = 0; i < n; ++i) {
        x = (x >> 1) ^ (-(x & 1u) & POLY) ^ ADD;
    }
    return x;
}
int main(void) { for (unsigned s = 0; s < NSEEDS; ++s) for (unsigned c = 0; c < NCOUNTS; ++c) printf("%08" PRIx32 "\n", step((uint32_t)SEEDS[s], COUNTS[c])); return 0; }
