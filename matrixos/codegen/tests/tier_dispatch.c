#include "mx_tiers.h"
#include <stdint.h>
#include <stddef.h>
MX_TIER_DISPATCH(uint32_t, sum_u32, (const uint32_t *p, size_t n), (p, n))
MX_TIER_DISPATCH(int, which_tier, (void), ())
