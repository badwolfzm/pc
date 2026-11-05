/*
 * test_asset_store — runs against a scratch store it builds itself.
 * Usage: test_asset_store <scratch-dir>   (created; must not be on a FAT/9p mount)
 */
#define _GNU_SOURCE
#include "asset_store.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int fails = 0, passes = 0;
#define CHECK(cond, msg) do { if (cond) { passes++; printf("  ok   %s\n", msg); } else { fails++; printf("  FAIL %s (%s:%d)\n", msg, __FILE__, __LINE__); } } while (0)

static void msleep(int ms) { struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L }; nanosleep(&ts, NULL); }

static int run(const char *cmd) { int rc = system(cmd); return WIFEXITED(rc) ? WEXITSTATUS(rc) : -1; }

/* Wait for events, polling like a frame loop would. Returns count collected. */
static size_t wait_events(asset_store *s, asset_event *ev, size_t max, int timeout_ms) {
    size_t n = 0;
    for (int t = 0; t < timeout_ms && n == 0; t += 5) { n = asset_poll(s, ev, max); if (!n) msleep(5); }
    return n;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <scratch-dir>\n", argv[0]); return 2; }
    const char *dir = argv[1];
    char cmd[8192], path[4096];
    snprintf(cmd, sizeof cmd, "rm -rf '%s' && mkdir -p '%s/store' '%s/outside'", dir, dir, dir);
    if (run(cmd) != 0) { perror("setup"); return 2; }

    /* --- 1. Pack a blob and an image with the real packer. --- */
    printf("packing\n");
    snprintf(path, sizeof path, "%s/blob.bin", dir);
    { FILE *f = fopen(path, "wb"); uint8_t buf[256]; for (int i = 0; i < 200 * 1024; ++i) { buf[i & 255] = (uint8_t)(i * 7 + (i >> 8)); if ((i & 255) == 255) fwrite(buf, 1, 256, f); } fclose(f); }
    snprintf(cmd, sizeof cmd, "./assetpack '%s/store' blob '%s/blob.bin' > '%s/blob.hash'", dir, dir, dir);
    CHECK(run(cmd) == 0, "assetpack blob");
    snprintf(path, sizeof path, "%s/img.raw", dir);
    { FILE *f = fopen(path, "wb"); for (unsigned y = 0; y < 64; ++y) for (unsigned x = 0; x < 64; ++x) { uint8_t px[4] = { (uint8_t)x, (uint8_t)y, (uint8_t)(x ^ y), 255 }; fwrite(px, 1, 4, f); } fclose(f); }
    snprintf(cmd, sizeof cmd, "./assetpack '%s/store' image '%s/img.raw' 64 64 --tiles > '%s/img.hash'", dir, dir, dir);
    CHECK(run(cmd) == 0, "assetpack image --tiles");

    char blob_hex[80] = {0}, img_hex[80] = {0};
    snprintf(path, sizeof path, "%s/blob.hash", dir); { FILE *f = fopen(path, "r"); if (f) { if (fscanf(f, "%79s", blob_hex) != 1) blob_hex[0] = 0; fclose(f); } }
    snprintf(path, sizeof path, "%s/img.hash", dir);  { FILE *f = fopen(path, "r"); if (f) { if (fscanf(f, "%79s", img_hex) != 1) img_hex[0] = 0; fclose(f); } }
    uint8_t blob_h[32], img_h[32];
    CHECK(asset_hash_parse(blob_hex, blob_h) == ASSET_OK, "blob manifest hash parses");
    CHECK(asset_hash_parse(img_hex, img_h) == ASSET_OK, "image manifest hash parses");

    /* --- 2. Open the store, HDD plan, small budget so eviction is exercised. --- */
    asset_store_config cfg = { .hot_set_bytes = 300 * 1024, .plan = ASSET_PLAN_HDD };
    asset_store *s = NULL;
    snprintf(path, sizeof path, "%s/store", dir);
    CHECK(asset_store_open(path, &cfg, &s) == ASSET_OK && s, "store opens");
    if (!s) return 1;

    /* --- 3. Jail: symlink to ../ must fail (security review finding 1). --- */
    printf("jail\n");
    snprintf(path, sizeof path, "%s/outside/secret.txt", dir);
    { FILE *f = fopen(path, "w"); fputs("WARDEN-ASSET 1\nkind blob\nmips 1\nmip 0 1\nchunk 0000000000000000000000000000000000000000000000000000000000000000 1\n", f); fclose(f); }
    snprintf(cmd, sizeof cmd, "ln -s ../../outside/secret.txt '%s/store/manifests/link' && ln -s ../../outside '%s/store/chunks/zz' && ln -s /etc/passwd '%s/store/manifests/abs'", dir, dir, dir);
    CHECK(run(cmd) == 0, "planted symlinks (file to ../, dir to ../, absolute)");
    errno = 0; int fd = asset_store_open_beneath(s, "manifests/link");
    CHECK(fd < 0 && errno == ELOOP, "symlink file to ../ refused with ELOOP");
    errno = 0; fd = asset_store_open_beneath(s, "chunks/zz/secret.txt");
    CHECK(fd < 0 && errno == ELOOP, "path through symlinked directory refused with ELOOP");
    errno = 0; fd = asset_store_open_beneath(s, "manifests/abs");
    CHECK(fd < 0 && errno == ELOOP, "absolute symlink refused with ELOOP");
    errno = 0; fd = asset_store_open_beneath(s, "../outside/secret.txt");
    CHECK(fd < 0 && (errno == EXDEV || errno == ELOOP || errno == ENOENT), "../ escape refused");
    errno = 0; fd = asset_store_open_beneath(s, "/etc/passwd");
    CHECK(fd < 0 && errno == EINVAL, "absolute path refused before the syscall");
    errno = 0; fd = asset_store_open_beneath(s, "chunks");
    CHECK(fd < 0 && errno == ELOOP, "a directory is not a chunk");
    { static char longp[6000]; memset(longp, 'a', sizeof longp - 1); memcpy(longp, "chunks/", 7); longp[sizeof longp - 1] = 0;
      errno = 0; fd = asset_store_open_beneath(s, longp);
      CHECK(fd < 0 && errno == ENAMETOOLONG, "a 6000-byte path is refused with ENAMETOOLONG, not truncated");
      snprintf(cmd, sizeof cmd, "touch '%s/store/chunks/%.100s'", dir, longp + 7); run(cmd);   /* a file whose name is the first 100 chars: truncation would have hit it */
      errno = 0; fd = asset_store_open_beneath(s, longp);
      CHECK(fd < 0 && errno == ENAMETOOLONG, "still refused when a truncated spelling of it exists"); }
    /* And the manifest-level API refuses the same thing: hash of the symlink name path is not even reachable,
     * but a hash whose manifest file is a symlink must still fail. */
    { uint8_t fake[32]; char hex[65]; memset(fake, 0xab, 32); asset_hash_hex(fake, hex);
      snprintf(cmd, sizeof cmd, "ln -s ../../outside/secret.txt '%s/store/manifests/%s'", dir, hex); run(cmd);
      asset_handle h; int rc = asset_open(s, fake, &h);
      CHECK(rc == ASSET_ERR_ESCAPE, "asset_open on a symlinked manifest returns ASSET_ERR_ESCAPE"); }
    asset_store_stats st0; asset_store_stats_get(s, &st0);
    CHECK(st0.escapes_refused >= 5, "escapes are counted");

    /* --- 4. Hash verification. --- */
    printf("verify\n");
    { uint8_t fake[32]; char hex[65]; memset(fake, 0x11, 32); asset_hash_hex(fake, hex);
      snprintf(path, sizeof path, "%s/store/manifests/%s", dir, hex);
      FILE *f = fopen(path, "w"); fputs("WARDEN-ASSET 1\nkind blob\nmips 1\nmip 0 1\nchunk 1111111111111111111111111111111111111111111111111111111111111111 1\n", f); fclose(f);
      asset_handle h; CHECK(asset_open(s, fake, &h) == ASSET_ERR_HASH, "manifest whose bytes do not match its name is refused"); }
    asset_handle blob = 0, img = 0;
    CHECK(asset_open(s, blob_h, &blob) == ASSET_OK, "blob manifest opens and verifies");
    CHECK(asset_open(s, img_h, &img) == ASSET_OK, "image manifest opens and verifies");

    /* Corrupt one chunk of the blob on disk: the read must fail with ASSET_ERR_HASH, not deliver bad bytes. */
    {
        snprintf(cmd, sizeof cmd, "h=$(grep -m1 '^chunk ' '%s/store/manifests/%s' | cut -d' ' -f2); d=$(printf %%s \"$h\" | cut -c1-2); f=\"%s/store/chunks/$d/$h\"; chmod u+w \"$f\"; printf 'X' | dd of=\"$f\" bs=1 seek=100 conv=notrunc status=none; echo \"$f\" > '%s/corrupt.txt'", dir, blob_hex, dir, dir);
        run(cmd);
    }
    asset_event ev[64];
    CHECK(asset_want(s, blob, 0, 0) == ASSET_OK, "want blob mip 0 is accepted (non-blocking)");
    { const void *d; size_t l; CHECK(asset_map(s, blob, 0, &d, &l) == ASSET_NOT_READY, "map before resident answers NOT_READY, never blocks"); }
    size_t n = wait_events(s, ev, 64, 5000);
    int saw_fail = 0, saw_res = 0;
    for (size_t i = 0; i < n; ++i) { if (ev[i].kind == ASSET_EV_FAILED && ev[i].rc == ASSET_ERR_HASH) saw_fail = 1; if (ev[i].kind == ASSET_EV_RESIDENT) saw_res = 1; }
    /* The corrupted chunk may belong to the blob or the image; either way the blob read must not silently succeed with bad bytes. */
    snprintf(path, sizeof path, "%s/corrupt.txt", dir);
    char corrupt[4096] = {0}; { FILE *f = fopen(path, "r"); if (f) { if (!fgets(corrupt, sizeof corrupt, f)) corrupt[0] = 0; fclose(f); } }
    CHECK(n > 0, "a completion event arrived");
    CHECK(saw_fail && !saw_res, "corrupted chunk of the blob produced ASSET_EV_FAILED with ASSET_ERR_HASH and nothing became resident");
    { const void *d; size_t l; CHECK(asset_map(s, blob, 0, &d, &l) == ASSET_NOT_READY, "corrupted blob is not mappable"); }
    if (0) {
        const void *d; size_t l; CHECK(asset_map(s, blob, 0, &d, &l) == ASSET_OK && l == 200 * 1024, "blob resident with full length (corrupted chunk was not its)");
        uint8_t h[32]; asset_sha256(d, l, h); (void)h;
        /* Verify against the original file. */
        snprintf(path, sizeof path, "%s/blob.bin", dir); FILE *f = fopen(path, "rb"); uint8_t *orig = malloc(l); size_t r = fread(orig, 1, l, f); fclose(f);
        CHECK(r == l && memcmp(orig, d, l) == 0, "resident bytes equal the packed file");
        free(orig);
    } else {
        CHECK(saw_fail, "corrupted chunk produced ASSET_EV_FAILED with ASSET_ERR_HASH");
    }
    /* Repair the corrupted chunk by repacking (dedup rewrites nothing that matches, rewrites what differs). */
    snprintf(cmd, sizeof cmd, "rm -f \"$(cat '%s/corrupt.txt')\"; ./assetpack '%s/store' blob '%s/blob.bin' >/dev/null && ./assetpack '%s/store' image '%s/img.raw' 64 64 --tiles >/dev/null", dir, dir, dir, dir, dir);
    CHECK(run(cmd) == 0, "repack restores the chunk");

    /* --- 5. Residency, generation, non-blocking frame loop, eviction under budget. --- */
    printf("residency\n");
    asset_status qs;
    CHECK(asset_query(s, img, &qs) == ASSET_OK && qs.kind == ASSET_KIND_IMAGE && qs.width == 64 && qs.height == 64 && qs.stride_px == 64 && qs.mips == 7, "image status: 64x64 ARGB, 7 mips");
    uint32_t gen0 = qs.generation;
    CHECK(asset_want(s, img, 6, 0) == ASSET_OK && asset_want(s, img, 0, 1) == ASSET_OK, "want mip tail (6) at priority 0 and mip 0 at priority 1");
    size_t total = 0; int got0 = 0, got6 = 0;
    for (int t = 0; t < 400 && !(got0 && got6); ++t) {
        n = asset_poll(s, ev, 64); total += n;
        for (size_t i = 0; i < n; ++i) if (ev[i].kind == ASSET_EV_RESIDENT && ev[i].asset == img) { if (ev[i].mip == 0) got0 = 1; if (ev[i].mip == 6) got6 = 1; }
        if (!(got0 && got6)) msleep(5);
    }
    CHECK(got0 && got6, "both mips became resident through poll");
    CHECK(asset_query(s, img, &qs) == ASSET_OK && (qs.resident_mask & 0x41) == 0x41 && qs.generation > gen0, "resident mask has bits 0 and 6; generation advanced");
    { const void *d; size_t l; CHECK(asset_map(s, img, 0, &d, &l) == ASSET_OK && l == 64 * 64 * 4 && (((uintptr_t)d) & 3) == 0, "mip 0 maps, 16 KiB, 4-byte aligned");
      /* Tiled layout: tile (1,0) begins at byte 1024 and its first pixel is x=16,y=0 -> (16,0,16,255) */
      const uint8_t *p = (const uint8_t *)d + 1024; CHECK(p[0] == 16 && p[1] == 0 && p[2] == 16 && p[3] == 255, "mip 0 is 16x16 tiled, row-major, 1 KiB per tile"); }
    /* Stability: the pointer must not change until the next poll even if we drop it. */
    { const void *d1, *d2; size_t l; asset_map(s, img, 0, &d1, &l); asset_drop(s, img, 0); asset_map(s, img, 0, &d2, &l);
      CHECK(d1 == d2, "mapping stays valid after drop until poll");
      uint32_t g = qs.generation; n = asset_poll(s, ev, 64);
      int evicted = 0; for (size_t i = 0; i < n; ++i) if (ev[i].kind == ASSET_EV_EVICTED && ev[i].asset == img && ev[i].mip == 0) evicted = 1;
      asset_query(s, img, &qs);
      CHECK(evicted && !(qs.resident_mask & 1) && qs.generation > g, "poll evicts the dropped mip, reports it, bumps generation"); }
    /* Budget: the blob (200 KiB) plus image mip 0 (16 KiB) fit in 300 KiB; a second 200 KiB blob does not unless something lower-priority is evictable. */
    CHECK(asset_want(s, blob, 0, 5) == ASSET_OK, "want blob at priority 5");
    n = wait_events(s, ev, 64, 5000);
    asset_query(s, blob, &qs); CHECK(qs.resident_mask & 1, "blob resident (200 KiB)");
    asset_want(s, img, 0, 0);   /* 16 KiB at priority 0: fits beside the blob */
    n = wait_events(s, ev, 64, 5000);
    asset_query(s, img, &qs); CHECK(qs.resident_mask & 1, "image mip 0 resident beside the blob");
    /* Now a 1 MiB blob at priority 9: too big for the budget at all -> refused at want time. */
    snprintf(path, sizeof path, "%s/big.bin", dir);
    { FILE *f = fopen(path, "wb"); for (int i = 0; i < (1 << 20) / 256; ++i) { uint8_t b[256]; for (int j = 0; j < 256; ++j) b[j] = (uint8_t)(i + j * 3); fwrite(b, 1, 256, f); } fclose(f); }
    snprintf(cmd, sizeof cmd, "./assetpack '%s/store' blob '%s/big.bin' > '%s/big.hash'", dir, dir, dir); run(cmd);
    char big_hex[80] = {0}; snprintf(path, sizeof path, "%s/big.hash", dir); { FILE *f = fopen(path, "r"); if (f) { if (fscanf(f, "%79s", big_hex) != 1) big_hex[0] = 0; fclose(f); } }
    uint8_t big_h[32]; asset_handle big; asset_hash_parse(big_hex, big_h);
    CHECK(asset_open(s, big_h, &big) == ASSET_OK, "1 MiB blob manifest opens");
    CHECK(asset_want(s, big, 0, 9) == ASSET_ERR_BUDGET, "a mip larger than the whole budget is refused at want time");
    /* A second 200 KiB-class request at a better priority evicts the blob (priority 5) to make room. */
    snprintf(path, sizeof path, "%s/blob2.bin", dir);
    { FILE *f = fopen(path, "wb"); for (int i = 0; i < 200 * 1024; ++i) fputc((i * 13) & 255, f); fclose(f); }
    snprintf(cmd, sizeof cmd, "./assetpack '%s/store' blob '%s/blob2.bin' > '%s/blob2.hash'", dir, dir, dir); run(cmd);
    char b2_hex[80] = {0}; snprintf(path, sizeof path, "%s/blob2.hash", dir); { FILE *f = fopen(path, "r"); if (f) { if (fscanf(f, "%79s", b2_hex) != 1) b2_hex[0] = 0; fclose(f); } }
    uint8_t b2_h[32]; asset_handle b2; asset_hash_parse(b2_hex, b2_h);
    CHECK(asset_open(s, b2_h, &b2) == ASSET_OK && asset_want(s, b2, 0, 1) == ASSET_OK, "second blob wanted at priority 1");
    n = wait_events(s, ev, 64, 5000);
    int ev_evict = 0, ev_res = 0;
    for (size_t i = 0; i < n; ++i) { if (ev[i].kind == ASSET_EV_EVICTED && ev[i].asset == blob) ev_evict = 1; if (ev[i].kind == ASSET_EV_RESIDENT && ev[i].asset == b2) ev_res = 1; }
    asset_status q1, q2; asset_query(s, blob, &q1); asset_query(s, b2, &q2);
    CHECK(ev_evict && ev_res && !(q1.resident_mask & 1) && (q2.resident_mask & 1), "lower-priority blob evicted to admit the higher-priority one, both reported");
    /* Same again but the newcomer is less important: it must fail with BUDGET and evict nothing. */
    CHECK(asset_want(s, blob, 0, 7) == ASSET_OK, "re-want the first blob at a worse priority than everything resident");
    n = wait_events(s, ev, 64, 5000);
    int budget_fail = 0; for (size_t i = 0; i < n; ++i) if (ev[i].kind == ASSET_EV_FAILED && ev[i].asset == blob && ev[i].rc == ASSET_ERR_BUDGET) budget_fail = 1;
    asset_query(s, b2, &q2);
    CHECK(budget_fail && (q2.resident_mask & 1), "less important request fails with ASSET_ERR_BUDGET and evicts nothing");

    /* --- 6. Close while in flight must not crash or leak the completion. --- */
    printf("teardown\n");
    asset_drop(s, b2, 0); asset_poll(s, ev, 64);
    CHECK(asset_want(s, b2, 0, 0) == ASSET_OK && asset_close(s, b2) == ASSET_OK, "close with a request in flight");
    msleep(50); asset_poll(s, ev, 64);
    CHECK(asset_close(s, blob) == ASSET_OK && asset_close(s, img) == ASSET_OK && asset_close(s, big) == ASSET_OK, "close the rest");
    asset_store_stats st; asset_store_stats_get(s, &st);
    printf("  stats: chunks_read=%llu bytes_read=%llu hash_failures=%llu escapes_refused=%llu evictions=%llu\n",
           (unsigned long long)st.chunks_read, (unsigned long long)st.bytes_read, (unsigned long long)st.hash_failures,
           (unsigned long long)st.escapes_refused, (unsigned long long)st.evictions);
    asset_store_close(s);

    /* SSD plan smoke: 4 readers, same store. */
    asset_store_config cfg2 = { .plan = ASSET_PLAN_SSD, .readers = 4 };
    snprintf(path, sizeof path, "%s/store", dir);
    CHECK(asset_store_open(path, &cfg2, &s) == ASSET_OK, "SSD plan store opens with 4 readers");
    asset_handle hh[3]; asset_open(s, blob_h, &hh[0]); asset_open(s, b2_h, &hh[1]); asset_open(s, big_h, &hh[2]);
    for (int i = 0; i < 3; ++i) asset_want(s, hh[i], 0, (uint8_t)i);
    int res = 0; for (int t = 0; t < 1000 && res < 3; ++t) { n = asset_poll(s, ev, 64); for (size_t i = 0; i < n; ++i) if (ev[i].kind == ASSET_EV_RESIDENT) res++; if (res < 3) msleep(2); }
    CHECK(res == 3, "three assets resident in parallel under the SSD plan");
    asset_store_close(s);

    printf("\n%d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
