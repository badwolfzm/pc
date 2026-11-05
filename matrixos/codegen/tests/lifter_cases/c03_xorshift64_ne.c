/* xorshift64, for (i = 0; i != n; i++) with i declared outside and read after */
#include "common.h"
uint64_t step(uint64_t x, uint64_t n, uint64_t *iafter) {
    uint64_t i;
    for (i = 0; i != n; i++) {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
    }
    *iafter = i;
    return x;
}
int main(void) { for (unsigned s = 0; s < NSEEDS; ++s) for (unsigned c = 0; c < NCOUNTS; ++c) { uint64_t ia; uint64_t r = step(SEEDS[s], COUNTS[c], &ia); printf("%016" PRIx64 " %" PRIu64 "\n", r, ia); } return 0; }
