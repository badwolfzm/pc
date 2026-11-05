/* signed int counter with <=, start value from a variable, counter read after */
#include "common.h"
uint64_t step(uint64_t x, int from, int to, int *iafter) {
    int i;
    for (i = from; i <= to; ++i) { x = (x << 1) ^ (0u - (x >> 63) & 0x1Bull); }
    *iafter = i;
    return x;
}
int main(void) {
    int pairs[][2] = { {0, 0}, {0, 10}, {5, 3}, {-7, 7}, {-3, -3}, {100, 1099} };
    for (unsigned s = 0; s < NSEEDS; ++s) for (unsigned p = 0; p < sizeof pairs / sizeof pairs[0]; ++p) { int ia; uint64_t r = step(SEEDS[s], pairs[p][0], pairs[p][1], &ia); printf("%016" PRIx64 " %d\n", r, ia); }
    return 0;
}
