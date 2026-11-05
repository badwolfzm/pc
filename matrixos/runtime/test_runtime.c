#define _GNU_SOURCE
/* test_runtime.c - ring, render service, asset grants, and a two-process game.
 * usage: test_runtime <scratch-dir>   (needs ./assetpack next to the binary) */
#include "mx_render.h"
#include "mx_rcs_build.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/wait.h>

static int passed, failed;
#define CHECK(cond, name) do { if (cond) { passed++; } else { failed++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, name); } } while (0)

static int run(const char *cmd) { int rc = system(cmd); return WIFEXITED(rc) ? WEXITSTATUS(rc) : -1; }
static void nap(void) { struct timespec ts = { 0, 200000 }; nanosleep(&ts, NULL); }

/* --- a counting sink --- */
typedef struct { uint32_t tiles, frames; uint64_t bytes; uint32_t last_x, last_y; } count_sink;
static int cs_tile(mx_render_sink *k, uint32_t ci, uint32_t fno, uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint32_t *px, uint32_t stride) {
    (void)ci; (void)fno; (void)px; (void)stride; count_sink *c = k->user; c->tiles++; c->bytes += (uint64_t)w * h * 4; c->last_x = x; c->last_y = y; return 0;
}
static void cs_done(mx_render_sink *k, uint32_t ci, uint32_t fno, const gc_stats *st, uint32_t w, uint32_t h) { (void)ci; (void)fno; (void)st; (void)w; (void)h; ((count_sink *)k->user)->frames++; }

static mx_ring *new_ring(void) {
    mx_ring *r = mmap(NULL, sizeof *r, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (r == MAP_FAILED) { perror("mmap"); exit(2); }
    mx_ring_init(r, sizeof *r);
    return r;
}

/* Build a batch straight into the ring and publish it. Returns bytes or 0. */
static uint32_t send_batch(mx_ring *r, uint8_t kind, void (*fill)(mxb *, void *), void *arg) {
    uint8_t *p = mx_ring_reserve(r, RCS_MAX_BATCH_BYTES);
    if (!p) return 0;
    mxb b; mxb_begin(&b, p, RCS_MAX_BATCH_BYTES, kind); fill(&b, arg);
    uint32_t n = mxb_end(&b);
    if (n) mx_ring_publish(r, p, n);
    return n;
}
static rcs_result pump_one(mx_render_service *s, mx_ring *r) {
    rcs_result res; memset(&res, 0xAA, sizeof res);
    mx_render_pump(s);
    if (!mx_ring_get_result(r, &res)) { memset(&res, 0, sizeof res); res.status = 99; }
    return res;
}

/* ---------------- ring tests ---------------- */
static void test_ring(void) {
    printf("ring\n");
    mx_ring *r = new_ring();
    uint8_t out[RCS_MAX_BATCH_BYTES]; size_t len;
    CHECK(mx_ring_take_batch(r, out, &len) == MX_RING_EMPTY, "empty ring");

    /* roundtrip */
    uint8_t *p = mx_ring_reserve(r, 100); CHECK(p != NULL, "reserve");
    for (int i = 0; i < 100; ++i) p[i] = (uint8_t)i;
    mx_ring_publish(r, p, 100);
    CHECK(mx_ring_in_flight(r) == 1, "one in flight");
    CHECK(mx_ring_take_batch(r, out, &len) == MX_RING_BATCH && len == 100 && out[57] == 57, "take roundtrip");
    rcs_result rr = { RCS_RESULT_MAGIC, RCS_OK, 0xFFFFFFFFu, 0, 0, 0, 0, 0, 0 };
    mx_ring_put_result(r, &rr);
    rcs_result got; CHECK(mx_ring_get_result(r, &got) && got.magic == RCS_RESULT_MAGIC, "result roundtrip");
    CHECK(!mx_ring_get_result(r, &got), "no second result");

    /* torn batch: the service's copy is independent of later client writes */
    p = mx_ring_reserve(r, 64); memset(p, 1, 64); mx_ring_publish(r, p, 64);
    CHECK(mx_ring_take_batch(r, out, &len) == MX_RING_BATCH, "take");
    memset(p, 2, 64);
    CHECK(out[0] == 1 && out[63] == 1, "copy-out is immune to the client rewriting the ring");
    mx_ring_put_result(r, &rr); mx_ring_get_result(r, &got);

    /* forged descriptors */
    uint32_t head = atomic_load(&r->cmd_head);
    r->desc[head % MX_RING_DESCS] = (mx_ring_desc){ 0, RCS_MAX_BATCH_BYTES + 1 }; atomic_store(&r->cmd_head, head + 1);
    CHECK(mx_ring_take_batch(r, out, &len) == MX_RING_BAD_DESC, "len over the batch cap is refused");
    head++; r->desc[head % MX_RING_DESCS] = (mx_ring_desc){ MX_RING_DATA - 16, 32 }; atomic_store(&r->cmd_head, head + 1);
    CHECK(mx_ring_take_batch(r, out, &len) == MX_RING_BAD_DESC, "offset+len past the data area is refused");
    head++; r->desc[head % MX_RING_DESCS] = (mx_ring_desc){ 0xFFFFFFF0u, 32 }; atomic_store(&r->cmd_head, head + 1);
    CHECK(mx_ring_take_batch(r, out, &len) == MX_RING_BAD_DESC, "offset near UINT32_MAX does not wrap");
    head++; r->desc[head % MX_RING_DESCS] = (mx_ring_desc){ 8, 8 }; atomic_store(&r->cmd_head, head + 1);
    CHECK(mx_ring_take_batch(r, out, &len) == MX_RING_BAD_DESC, "shorter than a batch header is refused");
    head++; r->desc[head % MX_RING_DESCS] = (mx_ring_desc){ MX_RING_DATA - 16, 16 }; atomic_store(&r->cmd_head, head + 1);
    CHECK(mx_ring_take_batch(r, out, &len) == MX_RING_BATCH && len == 16, "the last 16 bytes of the area are still reachable");
    for (int i = 0; i < 5; ++i) mx_ring_put_result(r, &rr);
    while (mx_ring_get_result(r, &got)) {}
    CHECK(atomic_load(&r->cmd_head) == atomic_load(&r->cmd_tail), "all consumed after forgeries");

    /* full table, then wraparound of the data area with large batches */
    int n = 0; while (mx_ring_reserve(r, 16) != NULL && n < 1000) { mx_ring_publish(r, r->data + atomic_load(&r->data_wr), 16); n++; }
    CHECK(n == (int)MX_RING_DESCS, "descriptor table caps in-flight batches");
    for (int i = 0; i < n; ++i) { CHECK(mx_ring_take_batch(r, out, &len) == MX_RING_BATCH, "drain"); mx_ring_put_result(r, &rr); }
    while (mx_ring_get_result(r, &got)) {}
    uint64_t hin = 1469598103934665603ull, hout = hin;
    for (int i = 0; i < 40; ++i) {
        uint32_t sz = 200u * 1024u + (uint32_t)i;
        p = mx_ring_reserve(r, sz); CHECK(p != NULL, "reserve 200 KiB");
        if (!p) break;
        for (uint32_t j = 0; j < sz; j += 977) { p[j] = (uint8_t)(i * 31 + j); hin = (hin ^ p[j]) * 1099511628211ull; }
        mx_ring_publish(r, p, sz);
        if (i % 3 == 2) {   /* consume some, leaving pending batches across the wrap */
            for (int k = 0; k < 3; ++k) { CHECK(mx_ring_take_batch(r, out, &len) == MX_RING_BATCH && len == sz - 2 + (uint32_t)k, "wrap take");
                for (uint32_t j = 0; j < len; j += 977) hout = (hout ^ out[j]) * 1099511628211ull;
                mx_ring_put_result(r, &rr); }
            while (mx_ring_get_result(r, &got)) {}
        }
    }
    while (mx_ring_take_batch(r, out, &len) == MX_RING_BATCH) { for (uint32_t j = 0; j < len; j += 977) hout = (hout ^ out[j]) * 1099511628211ull; mx_ring_put_result(r, &rr); }
    CHECK(hin == hout, "bytes survive the data-area wraparound in order");
    munmap(r, sizeof *r);
}

/* ---------------- service without a store ---------------- */
static void f_create(mxb *b, void *a) { (void)a; mxb_create_surface(b, 0, 64, 48, RCS_FMT_ARGB8888); mxb_create_surface(b, 1, 16, 16, RCS_FMT_ARGB8888); }
static void f_upload(mxb *b, void *a) { mxb_upload(b, 1, (rcs_rect){ 0, 0, 16, 16 }, a, 16 * 16 * 4); }
static void f_frame1(mxb *b, void *a) { uint32_t f = *(uint32_t *)a; mxb_begin_frame(b, f); mxb_clear(b, 0, 0xFF202020u); mxb_blit(b, 0, 32, 16, 1, (rcs_rect){ 0, 0, 16, 16 }, RCS_BLEND_COPY); mxb_present(b, 0, f); mxb_fence(b, 7); }
static void f_small(mxb *b, void *a) { uint32_t f = *(uint32_t *)a; mxb_begin_frame(b, f); mxb_fill_rect(b, 0, (rcs_rect){ 4, 4, 8, 8 }, 0xFFFF0000u); mxb_present(b, 0, f); }
static void f_bad(mxb *b, void *a) { uint32_t f = *(uint32_t *)a; mxb_begin_frame(b, f); mxb_fill_rect(b, 5, (rcs_rect){ 0, 0, 8, 8 }, 1); mxb_present(b, 0, f); }
static void f_noright(mxb *b, void *a) { (void)a; mxb_begin_frame(b, 1); mxb_present(b, 0, 1); }

static void test_service(void) {
    printf("service\n");
    count_sink cs = { 0 }; mx_render_sink sink = { cs_tile, cs_done, &cs };
    mx_render_service s; CHECK(mx_render_init(&s, NULL, &sink) == 0, "init");
    mx_ring *r = new_ring();
    int ci = mx_render_attach(&s, r, 42, RCS_R_ALL); CHECK(ci == 0, "attach");
    rcs_result res;
    CHECK(send_batch(r, RCS_BATCH_RESOURCE, f_create, NULL) && (res = pump_one(&s, r)).status == RCS_OK, "create surfaces");
    uint32_t sprite[256]; for (int i = 0; i < 256; ++i) sprite[i] = 0xFF000000u | (uint32_t)(i * 0x010203);
    CHECK(send_batch(r, RCS_BATCH_RESOURCE, f_upload, sprite) && (res = pump_one(&s, r)).status == RCS_OK && res.upload_bytes == 1024, "upload sprite");

    uint32_t f = 1;
    CHECK(send_batch(r, RCS_BATCH_FRAME, f_frame1, &f) && (res = pump_one(&s, r)).status == RCS_OK, "first frame");
    CHECK(cs.frames == 1 && cs.tiles == 12, "first present is a key frame: all 4x3 tiles emitted");
    mx_render_client *c = &s.clients[ci];
    CHECK(c->canvas_live && c->canvas.w == 64 && c->canvas.h == 48, "canvas sized to the presented surface");
    CHECK(memcmp(c->canvas.ghost, c->rcs.surfaces[0].px, 64 * 48 * 4) == 0, "ghost == presented surface");
    CHECK(c->canvas.ghost[17 * 64 + 33] == sprite[1 * 16 + 1], "sprite pixel reached the display");
    uint64_t h1 = mx_render_display_hash(&s, ci); CHECK(h1 != 0, "display hash nonzero");

    f = 2; cs.tiles = 0;
    CHECK(send_batch(r, RCS_BATCH_FRAME, f_small, &f) && (res = pump_one(&s, r)).status == RCS_OK, "second frame");
    CHECK(cs.tiles == 1 && cs.last_x == 0 && cs.last_y == 0, "an 8x8 rect inside one tile emits exactly that tile");
    uint64_t h2 = mx_render_display_hash(&s, ci); CHECK(h2 != h1, "display changed");

    f = 3; cs.tiles = 0;
    CHECK(send_batch(r, RCS_BATCH_FRAME, f_small, &f) && (res = pump_one(&s, r)).status == RCS_OK, "third frame, same draw");
    CHECK(cs.tiles == 0 && c->canvas.last.tiles_false == 1, "redrawing identical pixels: validator damages the tile, canvas XOR drops it");
    CHECK(mx_render_display_hash(&s, ci) == h2, "display unchanged");

    f = 4; cs.tiles = 0; cs.frames = 0;
    CHECK(send_batch(r, RCS_BATCH_FRAME, f_bad, &f) && (res = pump_one(&s, r)).status == RCS_REJECTED && res.error == RCS_E_SLOT_EMPTY && res.failing_index == 1, "rejected batch names the command");
    CHECK(cs.frames == 0 && !c->rcs.in_frame && c->stats.rejected == 1, "rejected batch presented nothing and left no open frame");
    CHECK(mx_render_display_hash(&s, ci) == h2, "display untouched by a rejected batch");

    /* forged descriptor through the service: reported, counted, ring keeps going */
    uint32_t head = atomic_load(&r->cmd_head);
    r->desc[head % MX_RING_DESCS] = (mx_ring_desc){ 0, MX_RING_DATA }; atomic_store(&r->cmd_head, head + 1);
    res = pump_one(&s, r);
    CHECK(res.status == RCS_REJECTED && res.error == RCS_E_TOO_LARGE && res.failing_index == 0xFFFFFFFFu && c->stats.bad_descs == 1, "bad descriptor answered as an envelope rejection");
    f = 5; CHECK(send_batch(r, RCS_BATCH_FRAME, f_small, &f) && (res = pump_one(&s, r)).status == RCS_OK, "ring still works after a bad descriptor");

    /* a flood of forged heads is bounded per pump and never blocks the other client */
    mx_ring *r2 = new_ring(); int c2 = mx_render_attach(&s, r2, 43, RCS_R_ALL); CHECK(c2 == 1, "second client");
    atomic_store(&r->cmd_head, atomic_load(&r->cmd_head) + 100000u);
    CHECK(send_batch(r2, RCS_BATCH_RESOURCE, f_create, NULL) != 0, "client 2 publishes");
    uint32_t n = mx_render_pump(&s);
    CHECK(n == MX_RING_DESCS + 1, "pump processed at most one table of client 1's garbage plus client 2's batch");
    CHECK(mx_ring_get_result(r2, &res) && res.status == RCS_OK, "client 2 was served despite client 1 flooding");
    for (int i = 0; i < 2000 && atomic_load(&r->cmd_head) != atomic_load(&r->cmd_tail); ++i) { mx_render_pump(&s); while (mx_ring_get_result(r, &res)) {} }
    CHECK(atomic_load(&r->cmd_head) == atomic_load(&r->cmd_tail), "flood drained");

    /* rights */
    mx_ring *r3 = new_ring(); int c3 = mx_render_attach(&s, r3, 44, RCS_R_DRAW | RCS_R_RESOURCE); CHECK(c3 == 2, "third client without present");
    CHECK(send_batch(r3, RCS_BATCH_FRAME, f_noright, NULL) && (res = pump_one(&s, r3)).status == RCS_REJECTED && res.error == RCS_E_RIGHT_MISSING, "present without the right is refused");
    CHECK(s.clients[c3].stats.presents == 0, "no present counted");

    /* detach / revoke */
    mx_render_detach(&s, ci);
    CHECK(atomic_load(&r->revoked) == 1 && mx_ring_reserve(r, 16) == NULL, "revoked ring refuses new batches");
    CHECK(!s.clients[ci].live && !s.clients[ci].canvas_live, "client freed");
    mx_render_free(&s);
    munmap(r, sizeof *r); munmap(r2, sizeof *r2); munmap(r3, sizeof *r3);
}

/* ---------------- service with the asset store ---------------- */
static int wait_ready(mx_render_service *s, mx_ring *r, uint32_t slot, int max_pumps) {
    for (int i = 0; i < max_pumps; ++i) { mx_render_pump(s); if (mx_ring_asset_ready(r, slot)) return i; nap(); }
    return -1;
}
static void f_create_img(mxb *b, void *a) { (void)a; mxb_create_surface(b, 0, 64, 64, RCS_FMT_ARGB8888); mxb_create_surface(b, 1, 64, 64, RCS_FMT_ARGB8888); }
static void f_bind(mxb *b, void *a) { (void)a; mxb_bind_asset(b, 0, 1); }
static void f_show(mxb *b, void *a) { uint32_t f = *(uint32_t *)a; mxb_begin_frame(b, f); mxb_blit(b, 0, 0, 0, 1, (rcs_rect){ 0, 0, 64, 64 }, RCS_BLEND_COPY); mxb_present(b, 0, f); }
static void f_tri(mxb *b, void *a) { uint32_t f = *(uint32_t *)a; mxb_begin_frame(b, f); mxb_clear(b, 0, 0xFF000000u); mxb_draw_triangles(b, 0, 1, 0, 2, RCS_SLOT_NONE, RCS_SHADE_FLAT); mxb_present(b, 0, f); }

static int read_hash(const char *path, uint8_t out[32]) { char hex[80] = { 0 }; FILE *f = fopen(path, "r"); if (!f) return -1; int ok = fscanf(f, "%79s", hex) == 1; fclose(f); return ok && asset_hash_parse(hex, out) == ASSET_OK ? 0 : -1; }

static void test_assets(const char *dir) {
    printf("assets\n");
    char cmd[8192], path[4096];
    snprintf(cmd, sizeof cmd, "rm -rf '%s' && mkdir -p '%s/store'", dir, dir); CHECK(run(cmd) == 0, "scratch");
    /* 64x64 image: gradient with alpha, stride == width (untiled: BIND_ASSET wants row-major) */
    snprintf(path, sizeof path, "%s/img.raw", dir);
    uint32_t img[64 * 64];
    { FILE *f = fopen(path, "wb"); for (unsigned y = 0; y < 64; ++y) for (unsigned x = 0; x < 64; ++x) { uint32_t v = 0xFF000000u | (x << 16) | (y << 8) | (x ^ y); img[y * 64 + x] = v; fwrite(&v, 4, 1, f); } fclose(f); }
    snprintf(cmd, sizeof cmd, "./assetpack '%s/store' image '%s/img.raw' 64 64 > '%s/img.hash'", dir, dir, dir); CHECK(run(cmd) == 0, "assetpack image");
    /* mesh: two triangles, 16.16 fixed */
    snprintf(path, sizeof path, "%s/mesh.bin", dir);
    { int32_t t[14] = { 2 << 16, 2 << 16, 60 << 16, 2 << 16, 2 << 16, 60 << 16, (int32_t)0xFF00FF00, 62 << 16, 62 << 16, 62 << 16, 10 << 16, 10 << 16, 62 << 16, (int32_t)0xFF0000FF };
      FILE *f = fopen(path, "wb"); fwrite(t, 4, 14, f); fclose(f); }
    snprintf(cmd, sizeof cmd, "./assetpack '%s/store' blob '%s/mesh.bin' > '%s/mesh.hash'", dir, dir, dir); CHECK(run(cmd) == 0, "assetpack mesh");
    /* a second image whose chunk we corrupt after packing */
    snprintf(path, sizeof path, "%s/bad.raw", dir);
    { FILE *f = fopen(path, "wb"); for (int i = 0; i < 64 * 64; ++i) { uint32_t v = 0xFFABCDEFu ^ (uint32_t)i; fwrite(&v, 4, 1, f); } fclose(f); }
    snprintf(cmd, sizeof cmd, "./assetpack '%s/store' image '%s/bad.raw' 64 64 > '%s/bad.hash'", dir, dir, dir); CHECK(run(cmd) == 0, "assetpack bad image");
    {   /* corrupt the first 16 KiB chunk of the bad image: chunks are named by their SHA-256 under chunks/xx/ */
        uint8_t badraw[16384]; snprintf(path, sizeof path, "%s/bad.raw", dir);
        FILE *f = fopen(path, "rb"); size_t n = f ? fread(badraw, 1, sizeof badraw, f) : 0; if (f) fclose(f);
        uint8_t h[32]; char hex[65]; asset_sha256(badraw, n, h); asset_hash_hex(h, hex);
        snprintf(path, sizeof path, "%s/store/chunks/%c%c/%s", dir, hex[0], hex[1], hex);
        f = fopen(path, "r+b"); int ok = f != NULL;
        if (f) { fseek(f, 100, SEEK_SET); fputc(0x01, f); fclose(f); }
        CHECK(ok, "corrupt one chunk of the bad image");
    }
    uint8_t img_h[32], mesh_h[32], bad_h[32];
    snprintf(path, sizeof path, "%s/img.hash", dir);  CHECK(read_hash(path, img_h) == 0, "image hash");
    snprintf(path, sizeof path, "%s/mesh.hash", dir); CHECK(read_hash(path, mesh_h) == 0, "mesh hash");
    snprintf(path, sizeof path, "%s/bad.hash", dir);  CHECK(read_hash(path, bad_h) == 0, "bad hash");

    asset_store_config cfg = { .hot_set_bytes = 8u << 20, .plan = ASSET_PLAN_SSD, .readers = 2 };
    asset_store *store = NULL; snprintf(path, sizeof path, "%s/store", dir);
    CHECK(asset_store_open(path, &cfg, &store) == ASSET_OK, "store opens"); if (!store) return;
    count_sink cs = { 0 }; mx_render_sink sink = { cs_tile, cs_done, &cs };
    mx_render_service s; mx_render_init(&s, store, &sink);
    mx_ring *r = new_ring(); int ci = mx_render_attach(&s, r, 1, RCS_R_ALL);

    CHECK(mx_render_grant_image(&s, ci, 0, img_h, 0, 0) == ASSET_OK, "grant image to slot 0");
    CHECK(mx_render_grant_mesh(&s, ci, 1, mesh_h, 0) == ASSET_OK, "grant mesh to slot 1");
    CHECK(mx_render_grant_mesh(&s, ci, 2, img_h, 0) == ASSET_ERR_FORMAT, "an image granted as a mesh is refused");
    CHECK(mx_render_grant_image(&s, ci, 3, mesh_h, 0, 0) == ASSET_ERR_FORMAT, "a blob granted as an image is refused");
    CHECK(mx_render_grant_image(&s, ci, 4, bad_h, 0, 0) == ASSET_OK, "corrupt image grants (manifest is intact)");
    CHECK(mx_render_grant_image(&s, ci, 0, img_h, 0, 0) == ASSET_ERR_ARG, "double grant refused");
    CHECK(s.clients[ci].rcs.assets[0].kind == RCS_ASSET_IMAGE && s.clients[ci].rcs.assets[0].w == 64 && s.clients[ci].rcs.assets[1].tri_count == 2, "asset table filled from the manifest");

    rcs_result res;
    CHECK(send_batch(r, RCS_BATCH_RESOURCE, f_create_img, NULL) && pump_one(&s, r).status == RCS_OK, "surfaces");
    /* Binding before residency is accepted by the validator and binds the stand-in (zeros): the
     * client is expected to wait on ready_mask. Prove both halves. */
    bool early = !mx_ring_asset_ready(r, 0);
    if (early) {
        CHECK(send_batch(r, RCS_BATCH_RESOURCE, f_bind, NULL) && pump_one(&s, r).status == RCS_OK, "bind before resident is accepted");
        if (!mx_ring_asset_ready(r, 0)) CHECK(s.clients[ci].rcs.surfaces[1].px[0] == 0, "...and binds the zero stand-in");
    }
    CHECK(wait_ready(&s, r, 0, 20000) >= 0, "image becomes resident");
    CHECK(wait_ready(&s, r, 1, 20000) >= 0, "mesh becomes resident");
    CHECK(send_batch(r, RCS_BATCH_RESOURCE, f_bind, NULL) && (res = pump_one(&s, r)).status == RCS_OK && res.bind_bytes == 64 * 64 * 4, "bind resident image");
    CHECK(memcmp(s.clients[ci].rcs.surfaces[1].px, img, sizeof img) == 0, "bound surface holds the verified asset bytes");
    uint32_t f = 1;
    CHECK(send_batch(r, RCS_BATCH_FRAME, f_show, &f) && pump_one(&s, r).status == RCS_OK, "present the asset");
    CHECK(memcmp(s.clients[ci].canvas.ghost, img, sizeof img) == 0 && cs.tiles == 16, "display shows the asset, 16 tiles for 64x64");
    f = 2;
    uint64_t before = mx_render_display_hash(&s, ci);
    CHECK(send_batch(r, RCS_BATCH_FRAME, f_tri, &f) && (res = pump_one(&s, r)).status == RCS_OK && res.triangles == 2, "draw the mesh");
    CHECK(mx_render_display_hash(&s, ci) != before && s.clients[ci].canvas.ghost[10 * 64 + 10] == 0xFF00FF00u && s.clients[ci].canvas.ghost[60 * 64 + 60] == 0xFF0000FFu, "triangles rendered from the streamed mesh");
    for (int i = 0; i < 300; ++i) { mx_render_pump(&s); nap(); }
    CHECK(!mx_ring_asset_ready(r, 4), "corrupted image never becomes ready (hash mismatch)");
    asset_store_stats st; asset_store_stats_get(store, &st); CHECK(st.hash_failures >= 1, "store counted the hash failure");
    mx_render_free(&s); asset_store_close(store); munmap(r, sizeof *r);
}

/* ---------------- two processes: the game in a child ---------------- */
static void f_game_res(mxb *b, void *a) { (void)a; mxb_create_surface(b, 0, 320, 180, RCS_FMT_ARGB8888); mxb_create_surface(b, 1, 16, 16, RCS_FMT_ARGB8888); }
typedef struct { uint32_t f; int x, y; } frame_arg;
static void f_game_frame(mxb *b, void *a) { frame_arg *g = a; mxb_begin_frame(b, g->f); if (g->f == 1) mxb_clear(b, 0, 0xFF103050u); mxb_fill_rect(b, 0, (rcs_rect){ g->x - 3, g->y, 16, 16 }, 0xFF103050u); mxb_blit(b, 0, g->x, g->y, 1, (rcs_rect){ 0, 0, 16, 16 }, RCS_BLEND_COPY); mxb_present(b, 0, g->f); }

static int child_game(mx_ring *r, int frames, bool hostile) {
    uint32_t sprite[256]; for (int i = 0; i < 256; ++i) sprite[i] = 0xFFFFFFFFu - (uint32_t)i;
    rcs_result res; int bad = 0;
    if (!send_batch(r, RCS_BATCH_RESOURCE, f_game_res, NULL)) return 10;
    if (!send_batch(r, RCS_BATCH_RESOURCE, f_upload, sprite)) return 11;
    for (int got = 0; got < 2;) { if (mx_ring_get_result(r, &res)) { if (res.status != RCS_OK) return 12; got++; } else nap(); }
    for (int i = 1; i <= frames; ++i) {
        frame_arg g = { (uint32_t)i, 3 * i, 40 };
        if (hostile && i == 10) {   /* publish a descriptor pointing outside the area, then carry on */
            uint32_t head = atomic_load(&r->cmd_head); r->desc[head % MX_RING_DESCS] = (mx_ring_desc){ MX_RING_DATA - 4, 64 }; atomic_store(&r->cmd_head, head + 1);
            for (;;) { if (mx_ring_get_result(r, &res)) break; nap(); }
            if (res.status != RCS_REJECTED) return 13;
        }
        while (!send_batch(r, RCS_BATCH_FRAME, f_game_frame, &g)) nap();
        for (;;) { if (mx_ring_get_result(r, &res)) break; nap(); }
        if (res.status != RCS_OK) bad++;
    }
    return bad;
}

static void test_two_process(void) {
    printf("two processes\n");
    for (int hostile = 0; hostile < 2; ++hostile) {
        count_sink cs = { 0 }; mx_render_sink sink = { cs_tile, cs_done, &cs };
        mx_render_service s; mx_render_init(&s, NULL, &sink);
        mx_ring *r = new_ring(); int ci = mx_render_attach(&s, r, 7, RCS_R_ALL);
        const int frames = 60;
        pid_t pid = fork();
        if (pid == 0) { _exit(child_game(r, frames, hostile != 0)); }
        int status = 0; uint64_t t0 = mx_now_ns();
        for (;;) {
            if (mx_render_pump(&s) == 0) { pid_t w = waitpid(pid, &status, WNOHANG); if (w == pid) break; nap(); }
            if (mx_now_ns() - t0 > 20000000000ull) { kill(pid, 9); waitpid(pid, &status, 0); break; }
        }
        mx_render_pump(&s);
        CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0, hostile ? "hostile child: every well-formed frame accepted" : "child game exited clean, every frame accepted");
        CHECK(s.clients[ci].stats.presents == (uint64_t)frames && cs.frames == (uint32_t)frames, "60 frames presented");
        CHECK(s.clients[ci].stats.bad_descs == (hostile ? 1u : 0u), hostile ? "one forged descriptor reported" : "no bad descriptors");
        CHECK(cs.tiles >= (uint32_t)(20 * 12 + 59 * 2) && cs.tiles <= (uint32_t)(20 * 12 + 59 * 6), "after the key frame each frame costs a few tiles, not the screen");
        /* the display equals what an in-process replay produces */
        mx_render_service s2; mx_render_init(&s2, NULL, NULL); mx_ring *r2 = new_ring(); int c2 = mx_render_attach(&s2, r2, 8, RCS_R_ALL);
        uint32_t sprite[256]; for (int i = 0; i < 256; ++i) sprite[i] = 0xFFFFFFFFu - (uint32_t)i;
        send_batch(r2, RCS_BATCH_RESOURCE, f_game_res, NULL); send_batch(r2, RCS_BATCH_RESOURCE, f_upload, sprite); mx_render_pump(&s2);
        for (int i = 1; i <= frames; ++i) { frame_arg g = { (uint32_t)i, 3 * i, 40 }; send_batch(r2, RCS_BATCH_FRAME, f_game_frame, &g); mx_render_pump(&s2); rcs_result rr; while (mx_ring_get_result(r2, &rr)) {} }
        CHECK(mx_render_display_hash(&s, ci) == mx_render_display_hash(&s2, c2) && mx_render_display_hash(&s, ci) != 0, "display hash matches the in-process replay");
        mx_render_free(&s); mx_render_free(&s2); munmap(r, sizeof *r); munmap(r2, sizeof *r2);
    }
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <scratch-dir>\n", argv[0]); return 2; }
    test_ring();
    test_service();
    test_assets(argv[1]);
    test_two_process();
    printf("test_runtime: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
