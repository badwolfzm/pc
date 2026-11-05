/* outer non-affine loop over seeds (left alone) containing an affine inner loop (lifted) */
#include "common.h"
#define XS(x) do { (x) ^= (x) << 13; (x) ^= (x) >> 17; (x) ^= (x) << 5; } while (0)
uint32_t table(uint32_t seed, unsigned n) {
    uint32_t acc = 0;
    for (unsigned s = 0; s < 16; ++s) {
        uint32_t x = seed + s * 0x9E3779B9u;
        for (unsigned k = 0; k < n; ++k) { x ^= x << 13; x ^= x >> 17; x ^= x << 5; }
        acc += x;
    }
    return acc;
}
int main(void) { for (unsigned c = 0; c < NCOUNTS; ++c) printf("%08" PRIx32 "\n", table(12345, (unsigned)COUNTS[c])); return 0; }
