/* Selects the super-op tier from $MXS_TIER (scalar|v128|v256|auto) before main().
 * Linked into the reference rcs_test when it is built against the SIMD backend. */
#include "../superops/mx_superops.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
__attribute__((constructor)) static void tier_from_env(void) {
    const char *t = getenv("MXS_TIER");
    mxs_tier want = MXS_TIER_AUTO;
    if (t && !strcmp(t, "scalar")) want = MXS_TIER_SCALAR;
    else if (t && !strcmp(t, "v128")) want = MXS_TIER_V128;
    else if (t && !strcmp(t, "v256")) want = MXS_TIER_V256;
    mxs_tier got = mxs_set_tier(want);
    fprintf(stderr, "[superops tier: %s]\n", mxs_tier_name(got));
}
