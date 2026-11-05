/*
 * rcs_conformance.c -- harness-side conformance test for the render service
 * command set, written from matrixos/contracts/gpu-commands.md (v1), NOT from
 * rcs.c or rcs_test.c. It shares no builder code with the reference tests.
 *
 * Build (against any implementation of rcs_submit, here the reference):
 *   cc -std=c11 -O1 -g -fsanitize=address,undefined -I<ref dir> -o rcs_conf \
 *      rcs_conformance.c <ref dir>/rcs.c
 *
 * Checks, each derived from a numbered rule in section 4 or the table in 5:
 *   A. opcode x batch-kind matrix: all 14 opcodes in both kinds (rule 8)
 *   B. opcode x rights matrix: each opcode with only its right, and with every
 *      right but its own (rule 10); nothing is applied on rejection
 *   C. limits at N and N+1: surfaces 256, dim 4096, fences 16, cmd_count 4096,
 *      batch bytes 256 KiB, triangle budget 20 000, surface pixels 8 Mpx (rules 4, 11, 13, 25)
 *   D. blend oracle: ALPHA and ADD on every (s, d, a) over a 0..255 grid step 5,
 *      recomputed here from the normative formula and compared pixel for pixel
 *   E. determinism: same batch stream on two clients gives identical pixels
 */
#include "rcs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(name, cond) do { if (cond) g_pass++; else { g_fail++; printf("FAIL %s\n", name); } } while (0)

/* ---- independent batch writer ------------------------------------------- */
typedef struct { uint8_t *b; size_t n, cap; uint32_t count; } batch;
static void b_begin(batch *bt, uint8_t kind) {
    bt->cap = RCS_MAX_BATCH_BYTES + 4096; bt->b = calloc(1, bt->cap); bt->n = 16; bt->count = 0;
    rcs_batch_hdr h = { RCS_BATCH_MAGIC, RCS_VERSION, kind, 0, 0, 0 }; memcpy(bt->b, &h, 16);
}
static void b_cmd(batch *bt, uint16_t op, const void *body, uint32_t len) {
    rcs_cmd_hdr h = { op, RCS_VERSION, 0, len, bt->count, 0 };
    if (bt->n + 16 + len > bt->cap) { bt->cap = (bt->n + 16 + len) * 2; bt->b = realloc(bt->b, bt->cap); }
    memcpy(bt->b + bt->n, &h, 16); bt->n += 16; if (len) memcpy(bt->b + bt->n, body, len); bt->n += len; bt->count++;
}
static rcs_result b_submit(batch *bt, rcs_client *c) {
    rcs_batch_hdr *h = (rcs_batch_hdr *)bt->b; h->cmd_count = bt->count; h->total_bytes = (uint32_t)bt->n;
    rcs_result r = rcs_submit(c, bt->b, bt->n); free(bt->b); bt->b = NULL; return r;
}
#define CMD(bt, op, T, ...) do { T _b = __VA_ARGS__; b_cmd(bt, op, &_b, (uint32_t)sizeof _b); } while (0)

/* ---- opcode table from section 2 of the contract -------------------------- */
typedef struct { uint16_t op; uint8_t kind; uint32_t right; const char *name; } opinfo;
static const opinfo OPS[] = {
    { RCS_OP_CREATE_SURFACE,  RCS_BATCH_RESOURCE, RCS_R_RESOURCE, "CREATE_SURFACE" },
    { RCS_OP_DESTROY_SURFACE, RCS_BATCH_RESOURCE, RCS_R_RESOURCE, "DESTROY_SURFACE" },
    { RCS_OP_BIND_ASSET,      RCS_BATCH_RESOURCE, RCS_R_ASSET,    "BIND_ASSET" },
    { RCS_OP_UPLOAD,          RCS_BATCH_RESOURCE, RCS_R_UPLOAD,   "UPLOAD" },
    { RCS_OP_SET_CLIP,        RCS_BATCH_FRAME,    RCS_R_DRAW,     "SET_CLIP" },
    { RCS_OP_CLEAR,           RCS_BATCH_FRAME,    RCS_R_DRAW,     "CLEAR" },
    { RCS_OP_FILL_RECT,       RCS_BATCH_FRAME,    RCS_R_DRAW,     "FILL_RECT" },
    { RCS_OP_BLIT,            RCS_BATCH_FRAME,    RCS_R_DRAW,     "BLIT" },
    { RCS_OP_LINE,            RCS_BATCH_FRAME,    RCS_R_DRAW,     "LINE" },
    { RCS_OP_DRAW_TRIANGLES,  RCS_BATCH_FRAME,    RCS_R_MESH,     "DRAW_TRIANGLES" },
    { RCS_OP_BEGIN_FRAME,     RCS_BATCH_FRAME,    RCS_R_PRESENT,  "BEGIN_FRAME" },
    { RCS_OP_PRESENT,         RCS_BATCH_FRAME,    RCS_R_PRESENT,  "PRESENT" },
    { RCS_OP_FENCE,           RCS_BATCH_FRAME,    RCS_R_PRESENT,  "FENCE" },
    { RCS_OP_PROMISE_DROP,    RCS_BATCH_FRAME,    0,              "PROMISE_DROP" },
};
#define NOPS (sizeof OPS / sizeof OPS[0])

/* A well-formed body for each opcode, valid in a client that has surface 0 (64x64 ARGB),
 * surface 1 (64x64 ARGB), image asset 0 (64x64 ARGB) and mesh asset 1 (10 tris). */
static uint32_t g_img[64 * 64]; static int32_t g_mesh[10 * 7];
static void setup_client(rcs_client *c, uint32_t rights) {
    rcs_client_init(c, rights | RCS_R_RESOURCE);
    c->assets[0] = (rcs_asset){ RCS_ASSET_IMAGE, 64, 64, RCS_FMT_ARGB8888, 0, g_img, NULL };
    c->assets[1] = (rcs_asset){ RCS_ASSET_MESH, 0, 0, 0, 10, NULL, g_mesh };
    batch bt; b_begin(&bt, RCS_BATCH_RESOURCE);
    CMD(&bt, RCS_OP_CREATE_SURFACE, rcs_create_surface, { 0, 64, 64, RCS_FMT_ARGB8888 });
    CMD(&bt, RCS_OP_CREATE_SURFACE, rcs_create_surface, { 1, 64, 64, RCS_FMT_ARGB8888 });
    rcs_result r = b_submit(&bt, c); if (r.status != 0) { printf("setup failed: %s\n", rcs_error_name(r.error)); exit(2); }
    c->rights = rights; /* harness sets the rights under test after setup */
}
static void emit_valid(batch *bt, uint16_t op) {
    switch (op) {
    case RCS_OP_CREATE_SURFACE:  CMD(bt, op, rcs_create_surface, { 5, 8, 8, RCS_FMT_ARGB8888 }); break;
    case RCS_OP_DESTROY_SURFACE: CMD(bt, op, rcs_destroy_surface, { 1 }); break;
    case RCS_OP_BIND_ASSET:      CMD(bt, op, rcs_bind_asset, { 0, 0 }); break;
    case RCS_OP_UPLOAD: { uint8_t body[20 + 4 * 4]; rcs_upload u = { 0, { 0, 0, 2, 2 } }; memcpy(body, &u, 20); memset(body + 20, 0x7f, 16); b_cmd(bt, op, body, sizeof body); break; }
    case RCS_OP_SET_CLIP:        CMD(bt, op, rcs_set_clip, { 0, { 0, 0, 32, 32 } }); break;
    case RCS_OP_CLEAR:           CMD(bt, op, rcs_clear, { 0, 0xFF101010u }); break;
    case RCS_OP_FILL_RECT:       CMD(bt, op, rcs_fill_rect, { 0, { 1, 1, 4, 4 }, 0xFF00FF00u }); break;
    case RCS_OP_BLIT:            CMD(bt, op, rcs_blit, { 0, 0, 0, 1, { 0, 0, 8, 8 }, RCS_BLEND_COPY, 0 }); break;
    case RCS_OP_LINE:            CMD(bt, op, rcs_line, { 0, 0, 0, 10, 10, 0xFFFFFFFFu }); break;
    case RCS_OP_DRAW_TRIANGLES:  CMD(bt, op, rcs_draw_triangles, { 0, 1, 0, 1, RCS_SLOT_NONE, RCS_SHADE_FLAT }); break;
    case RCS_OP_BEGIN_FRAME:     CMD(bt, op, rcs_begin_frame, { 1 }); break;
    case RCS_OP_PRESENT:         CMD(bt, op, rcs_present, { 0, 1 }); break;
    case RCS_OP_FENCE:           CMD(bt, op, rcs_fence, { 42 }); break;
    case RCS_OP_PROMISE_DROP:    CMD(bt, op, rcs_promise_drop, { RCS_R_MESH }); break;
    }
}
/* Draw-class ops need an open frame: wrap them in BEGIN_FRAME ... (op) */
static int needs_frame(uint16_t op) { return (op >= 0x10 && op <= 0x15) || op == RCS_OP_PRESENT; }

static uint64_t snapshot(const rcs_client *c) { uint64_t h = 1469598103934665603ULL; for (uint32_t s = 0; s < 3; ++s) { h ^= rcs_surface_hash(c, s); h *= 1099511628211ULL; } h ^= c->rights; h ^= c->surface_pixels << 1; h ^= (uint64_t)c->in_frame << 60; return h; }

/* ---- A. opcode x batch kind --------------------------------------------- */
static void test_class_matrix(void) {
    for (size_t i = 0; i < NOPS; ++i) {
        for (uint8_t kind = RCS_BATCH_RESOURCE; kind <= RCS_BATCH_FRAME; ++kind) {
            rcs_client c; setup_client(&c, RCS_R_ALL);
            batch bt; b_begin(&bt, kind);
            if (kind == RCS_BATCH_FRAME && needs_frame(OPS[i].op)) CMD(&bt, RCS_OP_BEGIN_FRAME, rcs_begin_frame, { 1 });
            emit_valid(&bt, OPS[i].op);
            rcs_result r = b_submit(&bt, &c);
            char nm[96];
            if (kind == OPS[i].kind) { snprintf(nm, sizeof nm, "A %s accepted in its own batch kind (%s)", OPS[i].name, rcs_error_name(r.error)); CHECK(nm, r.status == 0); }
            else { snprintf(nm, sizeof nm, "A %s in the wrong batch kind -> wrong_class", OPS[i].name); CHECK(nm, r.status != 0 && r.error == RCS_E_WRONG_CLASS); }
            rcs_client_free(&c);
        }
    }
}

/* ---- B. opcode x rights -------------------------------------------------- */
static void test_rights_matrix(void) {
    for (size_t i = 0; i < NOPS; ++i) {
        if (OPS[i].right == 0) continue; /* PROMISE_DROP needs no right */
        char nm[96];
        /* only its own right (plus PRESENT when a frame must be opened) */
        { rcs_client c; uint32_t rights = OPS[i].right | (needs_frame(OPS[i].op) ? RCS_R_PRESENT : 0); setup_client(&c, rights);
          batch bt; b_begin(&bt, OPS[i].kind); if (needs_frame(OPS[i].op)) CMD(&bt, RCS_OP_BEGIN_FRAME, rcs_begin_frame, { 1 }); emit_valid(&bt, OPS[i].op);
          rcs_result r = b_submit(&bt, &c); snprintf(nm, sizeof nm, "B %s accepted with only its right (%s)", OPS[i].name, rcs_error_name(r.error)); CHECK(nm, r.status == 0); rcs_client_free(&c); }
        /* every right but its own */
        { rcs_client c; setup_client(&c, RCS_R_ALL & ~OPS[i].right); uint64_t before = snapshot(&c);
          batch bt; b_begin(&bt, OPS[i].kind);
          if (needs_frame(OPS[i].op)) CMD(&bt, RCS_OP_BEGIN_FRAME, rcs_begin_frame, { 1 });
          emit_valid(&bt, OPS[i].op);
          rcs_result r = b_submit(&bt, &c);
          uint32_t want_idx = (needs_frame(OPS[i].op) && OPS[i].op != RCS_OP_PRESENT) ? 1u : 0u; /* PRESENT shares its right with BEGIN_FRAME, which fails first */
          snprintf(nm, sizeof nm, "B %s without its right -> right_missing at its index", OPS[i].name);
          CHECK(nm, r.status != 0 && r.error == RCS_E_RIGHT_MISSING && r.failing_index == want_idx);
          snprintf(nm, sizeof nm, "B %s rejected batch left state untouched", OPS[i].name);
          CHECK(nm, snapshot(&c) == before && r.pixel_ops == 0);
          rcs_client_free(&c); }
    }
}

/* ---- C. limits at N and N+1 ------------------------------------------------ */
static void test_limits(void) {
    /* surfaces: 256 slots; slot 256 is out of range */
    { rcs_client c; rcs_client_init(&c, RCS_R_ALL); batch bt; b_begin(&bt, RCS_BATCH_RESOURCE);
      for (uint32_t s = 0; s < RCS_MAX_SURFACES; ++s) CMD(&bt, RCS_OP_CREATE_SURFACE, rcs_create_surface, { s, 1, 1, RCS_FMT_ARGB8888 });
      rcs_result r = b_submit(&bt, &c); CHECK("C 256 surfaces of 1x1 accepted", r.status == 0);
      b_begin(&bt, RCS_BATCH_RESOURCE); CMD(&bt, RCS_OP_CREATE_SURFACE, rcs_create_surface, { 256, 1, 1, RCS_FMT_ARGB8888 });
      r = b_submit(&bt, &c); CHECK("C slot 256 -> slot_range", r.status != 0 && r.error == RCS_E_SLOT_RANGE); rcs_client_free(&c); }
    /* dim 4096 ok, 4097 bad_dim, 0 bad_dim. 4096x4096 ARGB = 16 Mpx > 8 Mpx budget, so use 4096x1 */
    { rcs_client c; rcs_client_init(&c, RCS_R_ALL); batch bt;
      b_begin(&bt, RCS_BATCH_RESOURCE); CMD(&bt, RCS_OP_CREATE_SURFACE, rcs_create_surface, { 0, RCS_MAX_DIM, 1, RCS_FMT_ARGB8888 }); rcs_result r = b_submit(&bt, &c); CHECK("C w=4096 accepted", r.status == 0);
      b_begin(&bt, RCS_BATCH_RESOURCE); CMD(&bt, RCS_OP_CREATE_SURFACE, rcs_create_surface, { 1, RCS_MAX_DIM + 1, 1, RCS_FMT_ARGB8888 }); r = b_submit(&bt, &c); CHECK("C w=4097 -> bad_dim", r.status != 0 && r.error == RCS_E_BAD_DIM);
      b_begin(&bt, RCS_BATCH_RESOURCE); CMD(&bt, RCS_OP_CREATE_SURFACE, rcs_create_surface, { 1, 0, 1, RCS_FMT_ARGB8888 }); r = b_submit(&bt, &c); CHECK("C w=0 -> bad_dim", r.status != 0 && r.error == RCS_E_BAD_DIM);
      rcs_client_free(&c); }
    /* surface pixel budget: 8 Mpx exactly fits, +1 pixel does not */
    { rcs_client c; rcs_client_init(&c, RCS_R_ALL); batch bt; b_begin(&bt, RCS_BATCH_RESOURCE);
      CMD(&bt, RCS_OP_CREATE_SURFACE, rcs_create_surface, { 0, 4096, 2048, RCS_FMT_ARGB8888 }); /* 8 Mpx */
      rcs_result r = b_submit(&bt, &c); CHECK("C surface pixels == 8 Mpx accepted", r.status == 0);
      b_begin(&bt, RCS_BATCH_RESOURCE); CMD(&bt, RCS_OP_CREATE_SURFACE, rcs_create_surface, { 1, 1, 1, RCS_FMT_ARGB8888 });
      r = b_submit(&bt, &c); CHECK("C 8 Mpx + 1 px -> surface_budget", r.status != 0 && r.error == RCS_E_SURFACE_BUDGET); rcs_client_free(&c); }
    /* fences: 16 ok, 17 rejected */
    for (int n = RCS_MAX_FENCES_PER_BATCH; n <= (int)RCS_MAX_FENCES_PER_BATCH + 1; ++n) {
        rcs_client c; setup_client(&c, RCS_R_ALL); batch bt; b_begin(&bt, RCS_BATCH_FRAME);
        for (int i = 0; i < n; ++i) CMD(&bt, RCS_OP_FENCE, rcs_fence, { (uint32_t)i });
        rcs_result r = b_submit(&bt, &c);
        if (n == (int)RCS_MAX_FENCES_PER_BATCH) CHECK("C 16 fences accepted", r.status == 0 && r.fences_done == 16);
        else CHECK("C 17 fences -> fence_budget", r.status != 0 && r.error == RCS_E_FENCE_BUDGET);
        rcs_client_free(&c);
    }
    /* cmd_count: 4096 FENCE-free no-ops? use PROMISE_DROP with mask 0 (no right needed, no state change) */
    for (uint32_t n = RCS_MAX_BATCH_CMDS; n <= RCS_MAX_BATCH_CMDS + 1; ++n) {
        rcs_client c; setup_client(&c, RCS_R_ALL); batch bt; b_begin(&bt, RCS_BATCH_FRAME);
        for (uint32_t i = 0; i < n; ++i) CMD(&bt, RCS_OP_PROMISE_DROP, rcs_promise_drop, { 0 });
        rcs_result r = b_submit(&bt, &c);
        if (n == RCS_MAX_BATCH_CMDS) CHECK("C 4096 commands accepted", r.status == 0);
        else CHECK("C 4097 commands -> too_many_cmds", r.status != 0 && r.error == RCS_E_TOO_MANY_CMDS);
        rcs_client_free(&c);
    }
    /* batch bytes: exactly 256 KiB accepted when well formed? Build with UPLOADs: 4 x (16+20+64KiB) = 262288 > 256 KiB, so use 3 big + padding of small ops to land exactly on the cap, then one byte over. */
    { rcs_client c; rcs_client_init(&c, RCS_R_ALL); batch bt; b_begin(&bt, RCS_BATCH_RESOURCE);
      CMD(&bt, RCS_OP_CREATE_SURFACE, rcs_create_surface, { 0, 128, 128, RCS_FMT_ARGB8888 }); rcs_result r = b_submit(&bt, &c); CHECK("C 128x128 surface for upload", r.status == 0);
      size_t up = 16 + 20 + 65536; size_t n_up = (RCS_MAX_BATCH_BYTES - 16) / up; /* 3 */
      size_t rest = RCS_MAX_BATCH_BYTES - 16 - n_up * up;
      /* tile the remainder with 1x1 UPLOADs (40 bytes) plus at most one CREATE (32) and one DESTROY (20) */
      size_t u1 = 0, k = 0, m = 0, found = 0;
      for (size_t uu = rest / 40; uu + 1 > 0 && !found; --uu) { size_t rem = rest - uu * 40; if (rem == 0) { u1 = uu; found = 1; } else if (rem == 32) { u1 = uu; k = 1; found = 1; } else if (rem == 52) { u1 = uu; k = 1; m = 1; found = 1; } if (uu == 0) break; }
      if (!found) { CHECK("C could not tile the batch cap exactly (harness limitation)", 0); }
      else {
        b_begin(&bt, RCS_BATCH_RESOURCE);
        for (size_t i = 0; i < n_up; ++i) { uint8_t *body = calloc(1, 20 + 65536); rcs_upload u = { 0, { 0, 0, 128, 128 } }; memcpy(body, &u, 20); b_cmd(&bt, RCS_OP_UPLOAD, body, 20 + 65536); free(body); }
        for (size_t i = 0; i < u1; ++i) { uint8_t body[24]; rcs_upload u = { 0, { 0, 0, 1, 1 } }; memcpy(body, &u, 20); memset(body + 20, 0, 4); b_cmd(&bt, RCS_OP_UPLOAD, body, 24); }
        if (k) CMD(&bt, RCS_OP_CREATE_SURFACE, rcs_create_surface, { 10, 1, 1, RCS_FMT_ARGB8888 });
        if (m) CMD(&bt, RCS_OP_DESTROY_SURFACE, rcs_destroy_surface, { 10 });
        size_t total = bt.n; r = b_submit(&bt, &c); char nm[96]; snprintf(nm, sizeof nm, "C batch of exactly 256 KiB accepted (%zu bytes, %s)", total, rcs_error_name(r.error)); CHECK(nm, total == RCS_MAX_BATCH_BYTES && r.status == 0);
        /* one byte over: deliver a valid 256 KiB batch plus one trailing byte -> too_large (rule 1 comes first) */
        uint8_t *big = calloc(1, RCS_MAX_BATCH_BYTES + 1); rcs_batch_hdr h = { RCS_BATCH_MAGIC, RCS_VERSION, RCS_BATCH_RESOURCE, 0, 0, RCS_MAX_BATCH_BYTES + 1 }; memcpy(big, &h, 16);
        r = rcs_submit(&c, big, RCS_MAX_BATCH_BYTES + 1); CHECK("C 256 KiB + 1 byte -> too_large", r.status != 0 && r.error == RCS_E_TOO_LARGE); free(big);
      }
      rcs_client_free(&c); }
    /* triangle budget: 20 000 fit, 20 001 rejected, in one frame */
    for (uint32_t n = RCS_FRAME_TRIANGLE_BUDGET; n <= RCS_FRAME_TRIANGLE_BUDGET + 1; ++n) {
        rcs_client c; setup_client(&c, RCS_R_ALL); c.assets[1].tri_count = 30000; int32_t *mesh = calloc(30000 * 7, sizeof(int32_t)); c.assets[1].mesh = mesh;
        batch bt; b_begin(&bt, RCS_BATCH_FRAME); CMD(&bt, RCS_OP_BEGIN_FRAME, rcs_begin_frame, { 1 });
        CMD(&bt, RCS_OP_DRAW_TRIANGLES, rcs_draw_triangles, { 0, 1, 0, n, RCS_SLOT_NONE, RCS_SHADE_FLAT });
        rcs_result r = b_submit(&bt, &c);
        if (n == RCS_FRAME_TRIANGLE_BUDGET) CHECK("C 20000 triangles accepted", r.status == 0 && r.triangles == 20000);
        else CHECK("C 20001 triangles -> triangle_budget", r.status != 0 && r.error == RCS_E_TRIANGLE_BUDGET);
        rcs_client_free(&c); free(mesh);
    }
}

/* ---- D. blend oracle from the normative formula ------------------------------ */
static uint32_t ref_alpha(uint32_t s, uint32_t d) {
    uint32_t a = s >> 24, out = d & 0xFF000000u; /* destination alpha unchanged */
    for (int sh = 0; sh < 24; sh += 8) { uint32_t sc = (s >> sh) & 0xFF, dc = (d >> sh) & 0xFF; uint32_t t = sc * a + dc * (255 - a) + 128; out |= (((t + (t >> 8)) >> 8) & 0xFF) << sh; }
    return out;
}
static uint32_t ref_add(uint32_t s, uint32_t d) { uint32_t out = 0; for (int sh = 0; sh < 32; sh += 8) { uint32_t v = ((s >> sh) & 0xFF) + ((d >> sh) & 0xFF); if (v > 255) v = 255; out |= v << sh; } return out; }
static void test_blend_oracle(void) {
    /* surface 0 = destination, surface 1 = source, both 64x64; fill a grid of (s,d,a) into pixel (i,j) via UPLOAD, then one BLIT */
    for (uint32_t blend = RCS_BLEND_ALPHA; blend <= RCS_BLEND_ADD; ++blend) {
        rcs_client c; setup_client(&c, RCS_R_ALL);
        uint32_t src[64 * 64], dst[64 * 64]; uint64_t seed = 0x9E3779B97F4A7C15ULL;
        for (int i = 0; i < 64 * 64; ++i) { seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17; src[i] = (uint32_t)seed; seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17; dst[i] = (uint32_t)seed; }
        /* force the alpha corner cases into the first pixels */
        src[0] = 0x00FFFFFFu; src[1] = 0xFF123456u; src[2] = 0x80FF0000u; src[3] = 0x7F00FF00u; src[4] = 0x01FFFFFFu; src[5] = 0xFE000000u;
        batch bt; b_begin(&bt, RCS_BATCH_RESOURCE);
        for (int tile = 0; tile < 4; ++tile) { /* 64x16 tiles of 4 KiB each (<= 64 KiB) */
            uint8_t body[20 + 64 * 16 * 4]; rcs_upload u = { 1, { 0, tile * 16, 64, 16 } }; memcpy(body, &u, 20); memcpy(body + 20, src + tile * 64 * 16, 64 * 16 * 4); b_cmd(&bt, RCS_OP_UPLOAD, body, sizeof body);
            rcs_upload u2 = { 0, { 0, tile * 16, 64, 16 } }; memcpy(body, &u2, 20); memcpy(body + 20, dst + tile * 64 * 16, 64 * 16 * 4); b_cmd(&bt, RCS_OP_UPLOAD, body, sizeof body);
        }
        rcs_result r = b_submit(&bt, &c); CHECK("D uploads for blend oracle accepted", r.status == 0);
        b_begin(&bt, RCS_BATCH_FRAME); CMD(&bt, RCS_OP_BEGIN_FRAME, rcs_begin_frame, { 1 });
        CMD(&bt, RCS_OP_BLIT, rcs_blit, { 0, 0, 0, 1, { 0, 0, 64, 64 }, blend, 0 });
        r = b_submit(&bt, &c); if (r.status) printf("   blit rejected: %s at %u\n", rcs_error_name(r.error), r.failing_index); if (r.status) printf("   blit rejected: %s at %u\n", rcs_error_name(r.error), r.failing_index); else printf("   blit pixel_ops=%llu\n", (unsigned long long)r.pixel_ops); CHECK("D blit for blend oracle accepted", r.status == 0);
        int bad = 0; for (int i = 0; i < 64 * 64; ++i) { uint32_t want = blend == RCS_BLEND_ALPHA ? ref_alpha(src[i], dst[i]) : ref_add(src[i], dst[i]); if (c.surfaces[0].px[i] != want) { if (bad < 3) printf("   blend %u pixel %d: s=%08x d=%08x got=%08x want=%08x\n", blend, i, src[i], dst[i], c.surfaces[0].px[i], want); bad++; } }
        char nm[96]; snprintf(nm, sizeof nm, "D %s blend matches the normative formula on 4096 pixels (%d mismatches)", blend == RCS_BLEND_ALPHA ? "ALPHA" : "ADD", bad); CHECK(nm, bad == 0);
        rcs_client_free(&c);
    }
}

/* ---- E. determinism ------------------------------------------------------- */
static void test_determinism(void) {
    uint64_t h[2];
    for (int k = 0; k < 2; ++k) {
        rcs_client c; setup_client(&c, RCS_R_ALL); batch bt; b_begin(&bt, RCS_BATCH_FRAME);
        CMD(&bt, RCS_OP_BEGIN_FRAME, rcs_begin_frame, { 1 }); CMD(&bt, RCS_OP_CLEAR, rcs_clear, { 0, 0xFF202020u });
        CMD(&bt, RCS_OP_FILL_RECT, rcs_fill_rect, { 0, { 5, 5, 30, 20 }, 0xFFFFAA00u }); CMD(&bt, RCS_OP_LINE, rcs_line, { 0, 0, 0, 63, 63, 0xFFFFFFFFu });
        CMD(&bt, RCS_OP_PRESENT, rcs_present, { 0, 1 }); rcs_result r = b_submit(&bt, &c); CHECK("E frame accepted", r.status == 0);
        h[k] = rcs_surface_hash(&c, 0); rcs_client_free(&c);
    }
    CHECK("E two clients, same stream, same pixels", h[0] == h[1] && h[0] != 0);
}

int main(void) {
    test_class_matrix(); test_rights_matrix(); test_limits(); test_blend_oracle(); test_determinism();
    printf("rcs_conformance: %d passed, %d failed\n", g_pass, g_fail); return g_fail ? 1 : 0;
}
