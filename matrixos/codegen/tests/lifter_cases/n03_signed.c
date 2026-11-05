/* NOT liftable: signed state */
#include "common.h"
int32_t step(int32_t x, uint64_t n) { for (uint64_t i = 0; i < n; ++i) x = (x >> 1) ^ (x & 1 ? 0x100 : 0); return x; }
int main(void) { printf("%08" PRIx32 "\n", (uint32_t)step(-77, 10)); return 0; }
