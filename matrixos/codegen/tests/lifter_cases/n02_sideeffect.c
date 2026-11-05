/* NOT liftable: a call in the body */
#include "common.h"
static int calls;
uint32_t step(uint32_t x, uint64_t n) { for (uint64_t i = 0; i < n; ++i) { x ^= x << 13; calls++; } return x; }
int main(void) { printf("%08" PRIx32 " %d\n", step(5, 10), calls); return 0; }
