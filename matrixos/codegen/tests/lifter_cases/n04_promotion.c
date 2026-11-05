/* NOT liftable as written: (~x) >> 1 on a uint8_t promotes to int and shifts sign bits down */
#include "common.h"
uint8_t step(uint8_t x, uint64_t n) { for (uint64_t i = 0; i < n; ++i) x = (uint8_t)((~x) >> 1) ^ 0x5a; return x; }
int main(void) { for (unsigned s = 0; s < NSEEDS; ++s) printf("%02x\n", step((uint8_t)SEEDS[s], 9)); return 0; }
