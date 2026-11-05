/*
 * test_asset_to_gpu — asset streaming from disk into the simulated GPU.
 *
 * Path under test:
 *   assetpack -> asset_store (openat2 jail, SHA-256 verify, residency)
 *     -> glue (this file): grant an RCS asset slot only for a resident mip
 *     -> rcs_submit(BIND_ASSET) (contracts/gpu-commands-ref, the RCS reference)
 *     -> surface pixels.
 *
 * The glue is the only place a store pointer becomes a GPU grant, so the
 * checks here are about that hand-off:
 *   1. a mip that is not resident is never granted and never bound;
 *   2. a streamed-in mip binds byte-for-byte to the GPU surface;
 *   3. a corrupt chunk (hash mismatch) never becomes resident and never binds;
 *   4. an evicted mip revokes its grant at the poll that reports it, and the
 *      GPU surface keeps the bytes it already bound (BIND copies);
 *   5. re-streaming after eviction binds again;
 *   6. the validator rejects a BIND for a slot the glue has not granted.
 *
 * Not covered: the ring transport, the real storage container, timing, and
 * the SIMD kernels. Those belong to other threads.
 *
 * Usage: test_asset_to_gpu <scratch-dir>   (created; run from a POSIX fs, not /mnt/c)
 */
#include "asset_store.h"
#include "../contracts/gpu-commands-ref/rcs.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define IMG_W 64u
#define IMG_H 64u
#define IMG_BYTES (IMG_W * IMG_H * 4u)

static int fails = 0, passes = 0;
#define CHECK(cond, msg) do { if (cond) { passes++; printf("  ok   %s\n", msg); } else { fails++; printf("  FAIL %s (%s:%d)\n", msg, __FILE__, __LINE__); } } while (0)

static int run(const char *cmd) { int rc = system(cmd); return WIFEXITED(rc) ? WEXITSTATUS(rc) : -1; }
static void msleep(int ms) { struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L }; nanosleep(&ts, NULL); }

/* ---- RCS batch builder (same wire layout as rcs_test.c) ---------------- */
typedef struct { uint8_t buf[2 * RCS_MAX_BATCH_BYTES]; size_t len; uint32_t count; } bb;

static void bb_begin(bb *b, uint8_t kind) {
    memset(b, 0, sizeof *b);
    rcs_batch_hdr h = { RCS_BATCH_MAGIC, RCS_VERSION, kind, 0, 0, 0 };
    memcpy(b->buf, &h, sizeof h); b->len = sizeof h; b->count = 0;
}
static void bb_add(bb *b, uint16_t op, const void *body, uint32_t body_len) {
    rcs_cmd_hdr h = { op, RCS_VERSION, 0, body_len, b->count, 0 };
    memcpy(b->buf + b->len, &h, sizeof h); b->len += sizeof h;
    memcpy(b->buf + b->len, body, body_len); b->len += body_len;
    b->count++;
}
static rcs_result submit(rcs_client *c, bb *b) {
    rcs_batch_hdr h; memcpy(&h, b->buf, sizeof h);
    h.cmd_count = b->count; h.total_bytes = (uint32_t)b->len;
    memcpy(b->buf, &h, sizeof h);
    return rcs_submit(c, b->buf, b->len);
}

/* ---- glue: store residency -> RCS asset grant -------------------------- */
typedef struct { bool used; asset_handle h; uint32_t mip; } grant_rec;
typedef struct {
    rcs_client *c;
    asset_store *s;
    grant_rec g[RCS_MAX_ASSETS];
} gpu_glue;

/* Grant asset slot `aslot` the resident mip and BIND it to surface `surf`.
 * Returns ASSET_OK, ASSET_NOT_READY (nothing granted, nothing submitted), or
 * an ASSET_ERR_* / RCS rejection mapped to ASSET_ERR_ARG. */
static int glue_bind(gpu_glue *G, asset_handle h, uint32_t mip, uint32_t aslot, uint32_t surf) {
    asset_status st;
    if (asset_query(G->s, h, &st) != ASSET_OK) return ASSET_ERR_ARG;
    if (!(st.resident_mask & (1u << mip))) return ASSET_NOT_READY;   /* never grant a non-resident mip */
    const void *p = NULL; size_t len = 0;
    int rc = asset_map(G->s, h, mip, &p, &len);
    if (rc != ASSET_OK) return rc;                                   /* NOT_READY if raced; never NULL-grant */
    if (!p || len != (size_t)st.width * st.height * 4u) return ASSET_ERR_FORMAT;

    G->c->assets[aslot] = (rcs_asset){ RCS_ASSET_IMAGE, st.width, st.height, RCS_FMT_ARGB8888, 0, (const uint32_t *)p, NULL };
    G->g[aslot] = (grant_rec){ true, h, mip };

    rcs_bind_asset ba = { aslot, surf };
    static bb b;
    bb_begin(&b, RCS_BATCH_RESOURCE);
    bb_add(&b, RCS_OP_BIND_ASSET, &ba, sizeof ba);
    rcs_result r = submit(G->c, &b);
    if (r.status != RCS_OK) return ASSET_ERR_ARG;
    return ASSET_OK;
}

/* Poll the store and revoke any grant whose bytes were just evicted or failed.
 * The grant pointer is only valid until this call, so revocation must happen
 * here, before the caller submits anything else. */
static size_t glue_poll(gpu_glue *G, asset_event *ev, size_t max) {
    size_t n = asset_poll(G->s, ev, max);
    for (size_t i = 0; i < n; ++i) {
        if (ev[i].kind != ASSET_EV_EVICTED && ev[i].kind != ASSET_EV_FAILED) continue;
        for (uint32_t a = 0; a < RCS_MAX_ASSETS; ++a) {
            if (G->g[a].used && G->g[a].h == ev[i].asset && G->g[a].mip == ev[i].mip) {
                G->c->assets[a] = (rcs_asset){0};
                G->g[a].used = false;
            }
        }
    }
    return n;
}

static bool wait_resident(gpu_glue *G, asset_handle h, uint32_t mip, int timeout_ms) {
    asset_event ev[16];
    for (int t = 0; t < timeout_ms; t += 5) {
        asset_status st;
        glue_poll(G, ev, 16);
        if (asset_query(G->s, h, &st) == ASSET_OK && (st.resident_mask & (1u << mip))) return true;
        msleep(5);
    }
    return false;
}

static bool wait_event(gpu_glue *G, asset_event_kind kind, asset_handle h, uint32_t mip, int timeout_ms) {
    asset_event ev[16];
    for (int t = 0; t < timeout_ms; t += 5) {
        size_t n = glue_poll(G, ev, 16);
        for (size_t i = 0; i < n; ++i)
            if (ev[i].kind == kind && ev[i].asset == h && ev[i].mip == mip) return true;
        msleep(5);
    }
    return false;
}

static bool surface_equals(const rcs_client *c, uint32_t slot, const uint8_t *bytes, size_t n) {
    return c->surfaces[slot].live && c->surfaces[slot].px && n == IMG_BYTES &&
           memcmp(c->surfaces[slot].px, bytes, n) == 0;
}

static bool read_file(const char *path, uint8_t *buf, size_t n) {
    FILE *f = fopen(path, "rb"); if (!f) return false;
    size_t got = fread(buf, 1, n, f); fclose(f); return got == n;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <scratch-dir>\n", argv[0]); return 2; }
    const char *dir = argv[1];
    char cmd[8192], path[4096];

    snprintf(cmd, sizeof cmd, "rm -rf '%s' && mkdir -p '%s/store' '%s/store_bad'", dir, dir, dir);
    if (run(cmd) != 0) { perror("setup"); return 2; }

    /* --- source image: 64x64 ARGB8888 with a pattern unique to this run --- */
    uint8_t *img_a = malloc(IMG_BYTES), *img_b = malloc(IMG_BYTES);
    if (!img_a || !img_b) return 2;
    for (uint32_t y = 0; y < IMG_H; ++y)
        for (uint32_t x = 0; x < IMG_W; ++x) {
            uint8_t *px = img_a + (y * IMG_W + x) * 4u;
            px[0] = (uint8_t)x; px[1] = (uint8_t)y; px[2] = (uint8_t)(x ^ y ^ 0x5a); px[3] = 255;
            uint8_t *qx = img_b + (y * IMG_W + x) * 4u;
            qx[0] = (uint8_t)(255 - x); qx[1] = (uint8_t)(y * 3); qx[2] = (uint8_t)(x * y); qx[3] = 255;
        }
    snprintf(path, sizeof path, "%s/a.raw", dir);
    { FILE *f = fopen(path, "wb"); fwrite(img_a, 1, IMG_BYTES, f); fclose(f); }
    snprintf(path, sizeof path, "%s/b.raw", dir);
    { FILE *f = fopen(path, "wb"); fwrite(img_b, 1, IMG_BYTES, f); fclose(f); }

    /* --- pack: good asset into store/, corrupt-to-be asset into store_bad/ --- */
    printf("packing\n");
    snprintf(cmd, sizeof cmd, "./assetpack '%s/store' image '%s/a.raw' %u %u > '%s/a.hash'", dir, dir, IMG_W, IMG_H, dir);
    CHECK(run(cmd) == 0, "assetpack image A (no tiles, full mip chain)");
    snprintf(cmd, sizeof cmd, "./assetpack '%s/store_bad' image '%s/b.raw' %u %u > '%s/b.hash'", dir, dir, IMG_W, IMG_H, dir);
    CHECK(run(cmd) == 0, "assetpack image B into the store that will be corrupted");
    /* Flip one byte in every chunk of store_bad. Manifests are left intact, so
     * the manifest hash still verifies and the failure is at the chunk read. */
    snprintf(cmd, sizeof cmd,
             "find '%s/store_bad/chunks' -type f -exec sh -c 'printf \"\\377\" | dd of=\"$1\" bs=1 seek=10 conv=notrunc 2>/dev/null' _ {} \\;",
             dir);
    CHECK(run(cmd) == 0, "corrupted every chunk of store_bad (one byte each)");

    char a_hex[80] = {0}, b_hex[80] = {0};
    snprintf(path, sizeof path, "%s/a.hash", dir); { FILE *f = fopen(path, "r"); if (f) { if (fscanf(f, "%79s", a_hex) != 1) a_hex[0] = 0; fclose(f); } }
    snprintf(path, sizeof path, "%s/b.hash", dir); { FILE *f = fopen(path, "r"); if (f) { if (fscanf(f, "%79s", b_hex) != 1) b_hex[0] = 0; fclose(f); } }
    uint8_t a_h[32], b_h[32];
    CHECK(asset_hash_parse(a_hex, a_h) == ASSET_OK, "asset A manifest hash parses");
    CHECK(asset_hash_parse(b_hex, b_h) == ASSET_OK, "asset B manifest hash parses");

    /* Reference bytes for mip 0 as the packer wrote them. */
    uint8_t *ref_a = malloc(IMG_BYTES);
    snprintf(path, sizeof path, "%s/a.raw", dir);
    if (!ref_a || !read_file(path, ref_a, IMG_BYTES)) { fprintf(stderr, "reference read failed\n"); return 2; }

    /* --- open both stores; budget is large enough for mip 0 + the whole chain --- */
    asset_store_config cfg = { .hot_set_bytes = 1024 * 1024, .plan = ASSET_PLAN_HDD };
    asset_store *sa = NULL, *sb = NULL;
    snprintf(path, sizeof path, "%s/store", dir);
    CHECK(asset_store_open(path, &cfg, &sa) == ASSET_OK && sa, "store A opens");
    snprintf(path, sizeof path, "%s/store_bad", dir);
    CHECK(asset_store_open(path, &cfg, &sb) == ASSET_OK && sb, "store B (corrupted) opens");
    if (!sa || !sb) return 1;

    /* One GPU client with a 64x64 surface in slot 0. */
    static rcs_client c;
    rcs_client_init(&c, RCS_R_ALL);
    gpu_glue Ga = { &c, sa, {{0}} };
    gpu_glue Gb = { &c, sb, {{0}} };
    static bb b;
    rcs_create_surface cs = { 0, IMG_W, IMG_H, RCS_FMT_ARGB8888 };
    bb_begin(&b, RCS_BATCH_RESOURCE); bb_add(&b, RCS_OP_CREATE_SURFACE, &cs, sizeof cs);
    CHECK(submit(&c, &b).status == RCS_OK, "GPU: surface 0 created (64x64 ARGB8888)");

    asset_handle ha = 0, hb = 0;
    CHECK(asset_open(sa, a_h, &ha) == ASSET_OK, "asset A opened (manifest verified)");
    CHECK(asset_open(sb, b_h, &hb) == ASSET_OK, "asset B opened (manifest verified)");

    /* --- 1. not resident: no grant, no bind, surface untouched ------------ */
    printf("residency\n");
    uint8_t zero[IMG_BYTES]; memset(zero, 0, sizeof zero);
    CHECK(surface_equals(&c, 0, zero, IMG_BYTES), "GPU surface starts as zeros");
    CHECK(glue_bind(&Ga, ha, 0, 3, 0) == ASSET_NOT_READY, "bind before any want -> NOT_READY (no grant)");
    CHECK(!c.assets[3].image && c.assets[3].kind == RCS_ASSET_NONE, "no grant recorded for slot 3");
    CHECK(surface_equals(&c, 0, zero, IMG_BYTES), "surface still zeros after refused bind");

    /* --- 2. stream from disk, then bind byte-for-byte --------------------- */
    CHECK(asset_want(sa, ha, 0, 0) == ASSET_OK, "asset A mip 0 wanted at priority 0");
    CHECK(wait_resident(&Ga, ha, 0, 5000), "asset A mip 0 became resident from disk");
    CHECK(glue_bind(&Ga, ha, 0, 3, 0) == ASSET_OK, "bind resident mip 0 to surface 0");
    CHECK(surface_equals(&c, 0, ref_a, IMG_BYTES), "GPU surface equals packed bytes of asset A, mip 0");

    /* Second read of the same mip is still mappable and identical. */
    const void *p2 = NULL; size_t l2 = 0;
    CHECK(asset_map(sa, ha, 0, &p2, &l2) == ASSET_OK && l2 == IMG_BYTES && memcmp(p2, ref_a, IMG_BYTES) == 0,
          "asset_map returns the verified bytes");

    /* --- 3. corrupt chunk: never resident, never bound -------------------- */
    printf("verify\n");
    CHECK(asset_want(sb, hb, 0, 0) == ASSET_OK, "corrupted asset B mip 0 wanted");
    CHECK(wait_event(&Gb, ASSET_EV_FAILED, hb, 0, 5000), "poll reports FAILED for corrupted chunk");
    asset_status stb;
    CHECK(asset_query(sb, hb, &stb) == ASSET_OK && !(stb.resident_mask & 1u), "corrupted mip not resident");
    CHECK(glue_bind(&Gb, hb, 0, 4, 0) == ASSET_NOT_READY, "bind of corrupted mip refused");
    CHECK(surface_equals(&c, 0, ref_a, IMG_BYTES), "surface still holds asset A after refused corrupt bind");

    /* --- 4. eviction revokes the grant; GPU keeps its own copy ------------ */
    printf("eviction\n");
    CHECK(asset_drop(sa, ha, 0) == ASSET_OK, "asset A mip 0 dropped");
    CHECK(wait_event(&Ga, ASSET_EV_EVICTED, ha, 0, 5000), "poll reports EVICTED for asset A mip 0");
    CHECK(!c.assets[3].image && c.assets[3].kind == RCS_ASSET_NONE, "grant for slot 3 revoked at the evicting poll");
    CHECK(asset_map(sa, ha, 0, &p2, &l2) == ASSET_NOT_READY, "asset_map on an evicted mip answers NOT_READY");
    CHECK(glue_bind(&Ga, ha, 0, 3, 0) == ASSET_NOT_READY, "bind of evicted mip refused");
    CHECK(surface_equals(&c, 0, ref_a, IMG_BYTES), "bound surface keeps its bytes after the store evicted the source");

    /* The validator rejects a BIND on a slot the glue never granted. */
    rcs_bind_asset ungranted = { 9, 0 };
    bb_begin(&b, RCS_BATCH_RESOURCE); bb_add(&b, RCS_OP_BIND_ASSET, &ungranted, sizeof ungranted);
    rcs_result rr = submit(&c, &b);
    CHECK(rr.status == RCS_REJECTED && rr.error == RCS_E_SLOT_EMPTY, "RCS rejects BIND of an ungranted slot (SLOT_EMPTY)");

    /* --- 5. re-stream after eviction binds again -------------------------- */
    printf("re-stream\n");
    CHECK(asset_want(sa, ha, 0, 0) == ASSET_OK, "asset A mip 0 re-wanted");
    CHECK(wait_resident(&Ga, ha, 0, 5000), "asset A mip 0 resident again");
    CHECK(glue_bind(&Ga, ha, 0, 3, 0) == ASSET_OK, "re-bind after re-stream");
    CHECK(surface_equals(&c, 0, ref_a, IMG_BYTES), "surface equals asset A after re-stream");

    asset_status sta;
    CHECK(asset_query(sa, ha, &sta) == ASSET_OK && sta.resident_mask != 0, "asset A reports a non-empty resident mask");

    /* --- wrap up ----------------------------------------------------------- */
    asset_store_close(sa);
    asset_store_close(sb);
    free(img_a); free(img_b); free(ref_a);
    rcs_client_free(&c);

    printf("\n%d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
