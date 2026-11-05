#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
static const uint64_t SEEDS[] = { 0, 1, 0x12345678u, 0xCAFEBABEu, 0xFFFFFFFFu, 0xDEADBEEFCAFEF00Dull, 0x8000000000000000ull, 77 };
static const uint64_t COUNTS[] = { 0, 1, 2, 3, 7, 8, 31, 32, 33, 100, 1000, 65535, 65536, 1000003 };
#define NSEEDS (sizeof SEEDS / sizeof SEEDS[0])
#define NCOUNTS (sizeof COUNTS / sizeof COUNTS[0])
