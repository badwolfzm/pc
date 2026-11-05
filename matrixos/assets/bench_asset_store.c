/*
 * bench_asset_store — measure what the store's read patterns cost on a given
 * drive. Builds N random blobs (default 64 x 4 MiB = 256 MiB) under <root>,
 * then times: (a) sequential read of one 4 MiB asset at a time in manifest
 * order (the HDD plan), (b) random 64 KiB chunk reads at queue depth 1 and at
 * the SSD plan's reader count, (c) verify-only throughput of SHA-256 on an
 * in-memory buffer (the CPU ceiling), all through the jailed reader.
 *
 * Cold numbers need the page cache dropped between runs; this harness cannot
 * do that without root, so it prints both the first (cold-ish) and second
 * (warm) pass and says which is which. On WSL2 the "disk" is a VHDX on NTFS;
 * treat results as that stack's numbers, not the bare drive's.
 *
 * Usage: bench_asset_store <root> [n_assets] [asset_mib]
 */
#define _GNU_SOURCE
#include "asset_store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static uint64_t now_ns(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (uint64_t)ts.tv_sec * 1000000000ull + ts.tv_nsec; }
static void msleep(int ms) { struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L }; nanosleep(&ts, NULL); }

static void drain(asset_store *s, int want) {
    asset_event ev[256]; int got = 0;
    while (got < want) { size_t n = asset_poll(s, ev, 256); for (size_t i = 0; i < n; ++i) if (ev[i].kind == ASSET_EV_RESIDENT || ev[i].kind == ASSET_EV_FAILED) got++; if (!n) msleep(1); }
}

static double run_plan(const char *root, asset_plan plan, unsigned readers, uint8_t (*hashes)[32], unsigned n, uint64_t *bytes, uint64_t *read_ns) {
    asset_store_config cfg = { .hot_set_bytes = (size_t)64 << 20, .plan = plan, .readers = readers, .max_assets = n + 1 };
    asset_store *s; if (asset_store_open(root, &cfg, &s) != ASSET_OK) { fprintf(stderr, "open failed\n"); exit(1); }
    asset_handle *h = calloc(n, sizeof *h);
    for (unsigned i = 0; i < n; ++i) if (asset_open(s, hashes[i], &h[i]) != ASSET_OK) { fprintf(stderr, "asset_open %u failed\n", i); exit(1); }
    uint64_t t0 = now_ns();
    /* Stream through the set: want each, poll until resident, drop, so the hot set never exceeds a few assets. */
    for (unsigned i = 0; i < n; ++i) { asset_want(s, h[i], 0, 0); drain(s, 1); asset_drop(s, h[i], 0); asset_event ev[8]; asset_poll(s, ev, 8); }
    uint64_t t1 = now_ns();
    asset_store_stats st; asset_store_stats_get(s, &st);
    *bytes = st.bytes_read; *read_ns = st.read_ns_total;
    free(h); asset_store_close(s);
    return (double)(t1 - t0) / 1e9;
}

/* Random 64 KiB chunk reads through the jail, issued from `readers` threads via the SSD plan on 64 KiB assets. */
static double run_random(const char *root, unsigned readers, uint8_t (*hashes)[32], unsigned n, uint64_t *bytes) {
    asset_store_config cfg = { .hot_set_bytes = (size_t)64 << 20, .plan = ASSET_PLAN_SSD, .readers = readers, .max_assets = n + 1, .max_pending = n + 16 };
    asset_store *s; if (asset_store_open(root, &cfg, &s) != ASSET_OK) exit(1);
    asset_handle *h = calloc(n, sizeof *h);
    for (unsigned i = 0; i < n; ++i) asset_open(s, hashes[i], &h[i]);
    /* Shuffle the order so the drive sees random positions. */
    unsigned *order = calloc(n, sizeof *order); for (unsigned i = 0; i < n; ++i) order[i] = i;
    srand(12345); for (unsigned i = n - 1; i > 0; --i) { unsigned j = (unsigned)rand() % (i + 1); unsigned t = order[i]; order[i] = order[j]; order[j] = t; }
    uint64_t t0 = now_ns();
    for (unsigned i = 0; i < n; ++i) asset_want(s, h[order[i]], 0, 0);
    drain(s, (int)n);
    uint64_t t1 = now_ns();
    asset_store_stats st; asset_store_stats_get(s, &st); *bytes = st.bytes_read;
    free(order); free(h); asset_store_close(s);
    return (double)(t1 - t0) / 1e9;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <root> [n_assets] [asset_mib]\n", argv[0]); return 2; }
    const char *root = argv[1];
    unsigned n = argc > 2 ? (unsigned)atoi(argv[2]) : 64;
    unsigned mib = argc > 3 ? (unsigned)atoi(argv[3]) : 4;
    char cmd[8192];
    snprintf(cmd, sizeof cmd, "mkdir -p '%s/src'", root); if (system(cmd) != 0) return 1;

    printf("building %u assets of %u MiB plus %u random 64 KiB chunks under %s\n", n, mib, n * 4, root);
    uint8_t (*big)[32] = calloc(n, 32), (*small)[32] = calloc(n * 4, 32);
    uint8_t *buf = malloc((size_t)mib << 20); uint64_t x = 0x9E3779B97F4A7C15ull;
    for (unsigned i = 0; i < n; ++i) {
        for (size_t k = 0; k < ((size_t)mib << 20); k += 8) { x ^= x << 13; x ^= x >> 7; x ^= x << 17; memcpy(buf + k, &x, 8); }
        char p[4096]; snprintf(p, sizeof p, "%s/src/big%u.bin", root, i);
        FILE *f = fopen(p, "wb"); fwrite(buf, 1, (size_t)mib << 20, f); fclose(f);
        snprintf(cmd, sizeof cmd, "./assetpack '%s' blob '%s' > '%s/src/big%u.hash'", root, p, root, i); if (system(cmd) != 0) return 1;
        snprintf(p, sizeof p, "%s/src/big%u.hash", root, i); f = fopen(p, "r"); char hex[80]; if (fscanf(f, "%79s", hex) != 1) return 1; fclose(f); asset_hash_parse(hex, big[i]);
    }
    for (unsigned i = 0; i < n * 4; ++i) {
        for (size_t k = 0; k < ASSET_CHUNK_BYTES; k += 8) { x ^= x << 13; x ^= x >> 7; x ^= x << 17; memcpy(buf + k, &x, 8); }
        char p[4096]; snprintf(p, sizeof p, "%s/src/small%u.bin", root, i);
        FILE *f = fopen(p, "wb"); fwrite(buf, 1, ASSET_CHUNK_BYTES, f); fclose(f);
        snprintf(cmd, sizeof cmd, "./assetpack '%s' blob '%s' > '%s/src/small%u.hash'", root, p, root, i); if (system(cmd) != 0) return 1;
        snprintf(p, sizeof p, "%s/src/small%u.hash", root, i); f = fopen(p, "r"); char hex[80]; if (fscanf(f, "%79s", hex) != 1) return 1; fclose(f); asset_hash_parse(hex, small[i]);
    }
    sync();

    printf("\n%-52s %10s %10s %10s\n", "pattern", "wall s", "MB/s", "pread MB/s");
    for (int pass = 0; pass < 2; ++pass) {
        uint64_t bytes, rns; double sec;
        const char *tag = pass == 0 ? "pass 1 (page cache as found)" : "pass 2 (warm)";
        sec = run_plan(root, ASSET_PLAN_HDD, 1, big, n, &bytes, &rns);
        printf("%-52s %10.3f %10.1f %10.1f\n", pass == 0 ? "HDD plan: sequential 4 MiB assets, 1 reader" : "  same, warm", sec, bytes / sec / 1e6, bytes / (rns / 1e9) / 1e6);
        sec = run_plan(root, ASSET_PLAN_SSD, 4, big, n, &bytes, &rns);
        printf("%-52s %10.3f %10.1f %10.1f\n", "SSD plan: same assets, 4 readers", sec, bytes / sec / 1e6, bytes / (rns / 1e9) / 1e6);
        sec = run_random(root, 1, small, n * 4, &bytes);
        printf("%-52s %10.3f %10.1f %10s\n", "random 64 KiB chunks, queue depth 1", sec, bytes / sec / 1e6, "");
        sec = run_random(root, 4, small, n * 4, &bytes);
        printf("%-52s %10.3f %10.1f %10s\n", "random 64 KiB chunks, 4 readers", sec, bytes / sec / 1e6, "");
        sec = run_random(root, 32, small, n * 4, &bytes);
        printf("%-52s %10.3f %10.1f %10s\n", "random 64 KiB chunks, 32 readers", sec, bytes / sec / 1e6, "");
        printf("  (%s)\n", tag);
    }
    /* CPU ceiling: SHA-256 over 256 MiB in memory. */
    { size_t len = (size_t)256 << 20; uint8_t *m = malloc(len); memset(m, 0x5a, len); uint8_t h[32];
      uint64_t t0 = now_ns(); for (size_t off = 0; off < len; off += ASSET_CHUNK_BYTES) asset_sha256(m + off, ASSET_CHUNK_BYTES, h); uint64_t t1 = now_ns();
      printf("%-52s %10.3f %10.1f %10s\n", "SHA-256 verify, in memory, one core (portable C)", (t1 - t0) / 1e9, len / ((t1 - t0) / 1e9) / 1e6, ""); free(m); }
    printf("\nNotes: MB/s are 1e6 bytes. 'pread MB/s' excludes open/verify. Cold numbers need `echo 3 > /proc/sys/vm/drop_caches` as root between passes.\n");
    free(buf); free(big); free(small);
    return 0;
}
