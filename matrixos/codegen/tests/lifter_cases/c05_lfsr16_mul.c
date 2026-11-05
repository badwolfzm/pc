/* 16-bit, tap select spelled as (x & 1) * POLY, bound given as N > i */
#include "common.h"
uint16_t step(uint16_t x, int n) {
    for (int i = 0; n > i; i = i + 1) { x = (x >> 1) ^ ((x & 1) * 0xB400); }
    return x;
}
int main(void) { for (unsigned s = 0; s < NSEEDS; ++s) for (unsigned c = 0; c < NCOUNTS; ++c) printf("%04x\n", step((uint16_t)SEEDS[s], (int)(COUNTS[c] & 0x7fffffff))); printf("%04x\n", step(0x1234, -5)); return 0; }
