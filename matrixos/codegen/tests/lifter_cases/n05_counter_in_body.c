/* NOT liftable: the body reads the counter */
#include "common.h"
uint32_t step(uint32_t x, uint32_t n) { for (uint32_t i = 0; i < n; ++i) x ^= (x << 3) ^ i; return x; }
int main(void) { printf("%08" PRIx32 "\n", step(1, 100)); return 0; }
