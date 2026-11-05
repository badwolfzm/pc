/* Sample SIMD kernel compiled three times by mx_tier_build.sh.  The body is
 * plain C so the compiler's own auto-vectorisation shows the tier (ymm/zmm
 * registers in the avx2/avx512 objects, none in baseline). */
#include "mx_tiers.h"
#include <stdint.h>
#include <stddef.h>
uint32_t MX_TIER_FN(sum_u32)(const uint32_t *p, size_t n) {
    uint32_t s = 0;
    for (size_t i = 0; i < n; ++i) s += p[i] * 2654435761u;
    return s;
}
int MX_TIER_FN(which_tier)(void) { return MX_TIER; }
