#include "mx_tiers.h"
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
uint32_t sum_u32(const uint32_t *p, size_t n); int which_tier(void);
int main(void) {
    static uint32_t buf[4099]; for (size_t i = 0; i < 4099; ++i) buf[i] = (uint32_t)(i * 7919u);
    uint32_t s = sum_u32(buf, 4099);
    printf("tier=%s which=%d sum=%08x\n", mx_tier_name(mx_tier_detect()), which_tier(), s);
    return 0;
}
