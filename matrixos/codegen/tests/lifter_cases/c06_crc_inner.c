/* CRC-32 bit loop: constant trip count 8 inside a byte loop; only the inner loop is affine */
#include "common.h"
uint32_t crc32(const uint8_t *p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t k = 0; k < n; ++k) {
        c ^= p[k];
        for (int j = 0; j < 8; ++j) c = (c >> 1) ^ (0xEDB88320u & -(c & 1u));
    }
    return ~c;
}
int main(void) { uint8_t buf[300]; for (int i = 0; i < 300; ++i) buf[i] = (uint8_t)(i * 7 + 3); for (size_t n = 0; n <= 300; n += 37) printf("%08" PRIx32 "\n", crc32(buf, n)); printf("%08" PRIx32 "\n", crc32((const uint8_t *)"123456789", 9)); return 0; }
