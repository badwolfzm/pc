/* Unit tests for mx_tiers.h: the feature->tier rule, the env lowering rule,
 * and the payload pick rule.  Pure functions, synthetic feature sets. */
#include "../tiers/mx_tiers.h"
#include <stdio.h>
static int fails = 0, passes = 0;
#define CHECK(c, msg) do { if (c) passes++; else { fails++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); } } while (0)
static mx_cpu_features v3(void) { mx_cpu_features f = {0}; f.osxsave = f.xcr0_sse = f.xcr0_avx = true; f.avx = f.avx2 = f.fma = f.bmi1 = f.bmi2 = f.lzcnt = f.movbe = f.f16c = true; return f; }
static mx_cpu_features v4(void) { mx_cpu_features f = v3(); f.avx512f = f.avx512bw = f.avx512cd = f.avx512dq = f.avx512vl = true; f.xcr0_opmask = f.xcr0_zmm_hi256 = f.xcr0_hi16_zmm = true; return f; }
static const char *env_avx2(const char *n) { return !strcmp(n, "MX_TIER") ? "avx2" : NULL; }
static const char *env_base(const char *n) { return !strcmp(n, "MX_TIER") ? "baseline" : NULL; }
static const char *env_bad(const char *n) { return !strcmp(n, "MX_TIER") ? "turbo" : NULL; }
static const char *env_avx512(const char *n) { return !strcmp(n, "MX_TIER") ? "avx512" : NULL; }
int main(void) {
    mx_cpu_features f = {0};
    CHECK(mx_tier_from_features(&f) == MX_TIER_BASELINE, "no features -> baseline");
    f = v3(); CHECK(mx_tier_from_features(&f) == MX_TIER_AVX2, "full v3 -> avx2");
    f = v4(); CHECK(mx_tier_from_features(&f) == MX_TIER_AVX512, "full v4 -> avx512");
    /* every single v3 feature missing drops to baseline */
    bool *v3bits[] = {0}; (void)v3bits;
    { mx_cpu_features g = v3(); g.avx2 = false;  CHECK(mx_tier_from_features(&g) == MX_TIER_BASELINE, "no avx2 -> baseline"); }
    { mx_cpu_features g = v3(); g.fma = false;   CHECK(mx_tier_from_features(&g) == MX_TIER_BASELINE, "no fma -> baseline"); }
    { mx_cpu_features g = v3(); g.bmi2 = false;  CHECK(mx_tier_from_features(&g) == MX_TIER_BASELINE, "no bmi2 -> baseline"); }
    { mx_cpu_features g = v3(); g.lzcnt = false; CHECK(mx_tier_from_features(&g) == MX_TIER_BASELINE, "no lzcnt -> baseline"); }
    { mx_cpu_features g = v3(); g.xcr0_avx = false; CHECK(mx_tier_from_features(&g) == MX_TIER_BASELINE, "OS did not enable AVX state -> baseline"); }
    { mx_cpu_features g = v3(); g.osxsave = false;  CHECK(mx_tier_from_features(&g) == MX_TIER_BASELINE, "no OSXSAVE -> baseline"); }
    /* partial AVX-512 (e.g. Knights Landing shape: F+CD without BW/VL) is an avx2 machine */
    { mx_cpu_features g = v4(); g.avx512bw = false; CHECK(mx_tier_from_features(&g) == MX_TIER_AVX2, "no avx512bw -> avx2"); }
    { mx_cpu_features g = v4(); g.avx512vl = false; CHECK(mx_tier_from_features(&g) == MX_TIER_AVX2, "no avx512vl -> avx2"); }
    { mx_cpu_features g = v4(); g.xcr0_hi16_zmm = false; CHECK(mx_tier_from_features(&g) == MX_TIER_AVX2, "OS did not enable ZMM state -> avx2"); }
    /* payload pick */
    CHECK(mx_tier_payload_pick(0x7, MX_TIER_AVX512) == 2, "all present, avx512 machine -> avx512");
    CHECK(mx_tier_payload_pick(0x7, MX_TIER_AVX2) == 1, "all present, avx2 machine -> avx2");
    CHECK(mx_tier_payload_pick(0x7, MX_TIER_BASELINE) == 0, "all present, baseline machine -> baseline");
    CHECK(mx_tier_payload_pick(0x1, MX_TIER_AVX512) == 0, "only baseline -> baseline on any machine");
    CHECK(mx_tier_payload_pick(0x5, MX_TIER_AVX2) == 0, "baseline+avx512 on avx2 machine -> baseline");
    CHECK(mx_tier_payload_pick(0x4, MX_TIER_AVX2) == -1, "only avx512 on avx2 machine -> unusable");
    CHECK(mx_tier_payload_pick(0x0, MX_TIER_AVX512) == -1, "no payload -> unusable");
    /* names */
    CHECK(mx_tier_parse("avx2") == 1 && mx_tier_parse("avx512") == 2 && mx_tier_parse("baseline") == 0 && mx_tier_parse("native") == -1 && mx_tier_parse(NULL) == -1, "parse");
    CHECK(!strcmp(MX_TIER_MARCH[0], "-march=x86-64") && !strcmp(MX_TIER_MARCH[1], "-march=x86-64-v3") && !strcmp(MX_TIER_MARCH[2], "-march=x86-64-v4"), "flag strings");
    /* env lowers, never raises; garbage ignored */
    mx_tier hw = mx_tier_detect_with(NULL);
    CHECK(mx_tier_detect_with(env_base) == MX_TIER_BASELINE, "MX_TIER=baseline lowers to baseline");
    CHECK(mx_tier_detect_with(env_bad) == hw, "garbage MX_TIER ignored");
    CHECK(mx_tier_detect_with(env_avx512) == hw, "MX_TIER=avx512 never raises above hardware");
    CHECK(mx_tier_detect_with(env_avx2) == (hw < MX_TIER_AVX2 ? hw : MX_TIER_AVX2), "MX_TIER=avx2 caps at avx2");
    printf("this machine: tier=%s\n", mx_tier_name(hw));
    fprintf(stderr, "tiers_test: %d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
