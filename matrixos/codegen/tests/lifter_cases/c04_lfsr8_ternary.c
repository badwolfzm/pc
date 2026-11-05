/* 8-bit LFSR with the ternary tap spelling; integer promotion must be handled */
#include "common.h"
uint8_t step(uint8_t x, uint32_t n) {
    for (uint32_t i = 0; i < n; i += 1) x = (x >> 1) ^ ((x & 1) ? 0xB8 : 0);
    return x;
}
int main(void) { for (unsigned s = 0; s < NSEEDS; ++s) for (unsigned c = 0; c < NCOUNTS; ++c) printf("%02x\n", step((uint8_t)SEEDS[s], (uint32_t)COUNTS[c])); return 0; }
