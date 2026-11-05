#define _POSIX_C_SOURCE 200809L
/* test_tiers.c -- the driver's tier flags must be exactly the codegen contract.
 * For each tier in mx_tiers.h, run `mxcc --dry-run --tier=<name> -c x.c` and
 * require MX_TIER_MARCH[t], MX_TIER_MTUNE and MX_TIER_DEFINE[t] to appear as
 * whole arguments, and no other -march= to appear. Usage: test_tiers <mxcc> */
#include "mx_tiers.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails = 0;

static int run_dry(const char *mxcc, const char *tier, char *out, size_t cap) {
    char cmd[4096];
    snprintf(cmd, sizeof cmd, "'%s' --dry-run --tier=%s -c x.c 2>&1", mxcc, tier);
    FILE *p = popen(cmd, "r");
    if (!p) return -1;
    size_t n = fread(out, 1, cap - 1, p); out[n] = 0;
    return pclose(p);
}

/* argv is printed one per line: match a whole line */
static bool has_arg(const char *out, const char *arg) {
    size_t n = strlen(arg);
    for (const char *p = out; (p = strstr(p, arg)); p += n)
        if ((p == out || p[-1] == '\n') && (p[n] == '\n' || p[n] == 0)) return true;
    return false;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: test_tiers <mxcc>\n"); return 2; }
    char out[65536];
    for (int t = 0; t < MX_TIER__COUNT; t++) {
        const char *name = MX_TIER_NAME[t];
        if (run_dry(argv[1], name, out, sizeof out) != 0) { fprintf(stderr, "%s: dry run failed:\n%s", name, out); fails++; continue; }
        #define NEED(s) do { if (!has_arg(out, (s))) { fprintf(stderr, "tier %s: missing %s\n", name, (s)); fails++; } } while (0)
        NEED(MX_TIER_MARCH[t]); NEED(MX_TIER_MTUNE); NEED(MX_TIER_DEFINE[t]);
        /* exactly one -march= and it is the contract's */
        int count = 0; for (const char *p = out; (p = strstr(p, "-march=")); p += 7) count++;
        if (count != 1) { fprintf(stderr, "tier %s: %d -march= flags, want 1\n", name, count); fails++; }
        if (strstr(out, "-march=native")) { fprintf(stderr, "tier %s: -march=native present\n", name); fails++; }
        /* the tier's own guard: the define must match what mx_tiers.h expects */
        char want[32]; snprintf(want, sizeof want, "-DMX_TIER=%d", t);
        if (strcmp(MX_TIER_DEFINE[t], want) != 0) { fprintf(stderr, "contract: MX_TIER_DEFINE[%d]=%s\n", t, MX_TIER_DEFINE[t]); fails++; }
        /* parse round-trips */
        if (mx_tier_parse(name) != t) { fprintf(stderr, "mx_tier_parse(%s) != %d\n", name, t); fails++; }
    }
    /* an unknown tier is refused by the driver */
    if (run_dry(argv[1], "sse9", out, sizeof out) == 0) { fprintf(stderr, "unknown tier accepted\n"); fails++; }
    printf("test_tiers: %d tiers checked against mx_tiers.h, %d failures\n", MX_TIER__COUNT, fails);
    return fails ? 1 : 0;
}
