/*
 * rcs_test.c - tests for the render service command set reference (rcs.c).
 * Build and run: make test   (gcc, -fsanitize=address,undefined)
 */
#include "rcs.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(expr) do { if (expr) g_pass++; else { g_fail++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); } } while (0)
#define CHECK_REJ(res, idx, err) do { rcs_result _r = (res); \
    bool ok = _r.status == RCS_REJECTED && _r.failing_index == (uint32_t)(idx) && _r.error == (err); \
    if (ok) g_pass++; else { g_fail++; fprintf(stderr, "FAIL %s:%d: expected reject idx=%d err=%s, got status=%u idx=%u err=%s\n", \
        __FILE__, __LINE__, (int)(idx), rcs_error_name(err), _r.status, _r.failing_index, rcs_error_name(_r.error)); } } while (0)
#define CHECK_OK(res) do { rcs_result _r = (res); if (_r.status == RCS_OK) g_pass++; else { g_fail++; \
    fprintf(stderr, "FAIL %s:%d: expected OK, got idx=%u err=%s\n", __FILE__, __LINE__, _r.failing_index, rcs_error_name(_r.error)); } } while (0)

/* ---- wire size contract (these numbers appear in gpu-commands.md) ---- */
_Static_assert(sizeof(rcs_batch_hdr) == 16, "batch header");
_Static_assert(sizeof(rcs_cmd_hdr) == 16, "cmd header");
_Static_assert(sizeof(rcs_create_surface) == 16, "");
_Static_assert(sizeof(rcs_destroy_surface) == 4, "");
_Static_assert(sizeof(rcs_bind_asset) == 8, "");
_Static_assert(sizeof(rcs_upload) == 20, "");
_Static_assert(sizeof(rcs_set_clip) == 20, "");
_Static_assert(sizeof(rcs_clear) == 8, "");
_Static_assert(sizeof(rcs_fill_rect) == 24, "");
_Static_assert(sizeof(rcs_blit) == 40, "");
_Static_assert(sizeof(rcs_line) == 24, "");
_Static_assert(sizeof(rcs_draw_triangles) == 24, "");
_Static_assert(sizeof(rcs_begin_frame) == 4, "");
_Static_assert(sizeof(rcs_present) == 8, "");
_Static_assert(sizeof(rcs_fence) == 4, "");
_Static_assert(sizeof(rcs_promise_drop) == 4, "");
_Static_assert(sizeof(rcs_result) == 40, "");

/* ---- batch builder ------------------------------------------------------- */
typedef struct { uint8_t buf[2 * RCS_MAX_BATCH_BYTES]; size_t len; uint32_t count; } bb;

static void bb_begin(bb *b, uint8_t kind) {
    memset(b, 0, sizeof *b);
    rcs_batch_hdr h = { RCS_BATCH_MAGIC, RCS_VERSION, kind, 0, 0, 0 };
    memcpy(b->buf, &h, sizeof h); b->len = sizeof h; b->count = 0;
}
static void bb_add_raw(bb *b, uint16_t op, const void *body, uint32_t body_len, const void *payload, uint32_t payload_len) {
    rcs_cmd_hdr h = { op, RCS_VERSION, 0, body_len + payload_len, b->count, 0 };
    memcpy(b->buf + b->len, &h, sizeof h); b->len += sizeof h;
    memcpy(b->buf + b->len, body, body_len); b->len += body_len;
    if (payload_len) { memcpy(b->buf + b->len, payload, payload_len); b->len += payload_len; }
    b->count++;
}
#define ADD(b, op, s) bb_add_raw((b), (op), &(s), sizeof(s), NULL, 0)
static void bb_finish(bb *b) {
    rcs_batch_hdr h; memcpy(&h, b->buf, sizeof h);
    h.cmd_count = b->count; h.total_bytes = (uint32_t)b->len;
    memcpy(b->buf, &h, sizeof h);
}
static rcs_result submit(rcs_client *c, bb *b) { bb_finish(b); return rcs_submit(c, b->buf, b->len); }

/* ---- fixtures -------------------------------------------------------------- */
static const uint32_t W = 1280, H = 720;

static rcs_result make_surfaces(rcs_client *c) {
    bb b; bb_begin(&b, RCS_BATCH_RESOURCE);
    rcs_create_surface s0 = {0, W, H, RCS_FMT_ARGB8888}; ADD(&b, RCS_OP_CREATE_SURFACE, s0);
    rcs_create_surface s1 = {1, 64, 64, RCS_FMT_ARGB8888}; ADD(&b, RCS_OP_CREATE_SURFACE, s1);
    rcs_create_surface s2 = {2, 64, 64, RCS_FMT_A8};       ADD(&b, RCS_OP_CREATE_SURFACE, s2);
    return submit(c, &b);
}

static void draw_scene(bb *b, uint32_t frame_no) {
    bb_begin(b, RCS_BATCH_FRAME);
    rcs_begin_frame bf = {frame_no}; ADD(b, RCS_OP_BEGIN_FRAME, bf);
    rcs_clear cl = {0, 0xFF202020u}; ADD(b, RCS_OP_CLEAR, cl);
    rcs_fill_rect fr = {0, {10, 10, 100, 40}, 0xFFFFAA00u}; ADD(b, RCS_OP_FILL_RECT, fr);
    rcs_blit bl = {0, 120, 20, 1, {0, 0, 64, 64}, RCS_BLEND_ALPHA, 0}; ADD(b, RCS_OP_BLIT, bl);
    rcs_line ln = {0, 0, 0, (int32_t)W - 1, (int32_t)H - 1, 0xFFFFFFFFu}; ADD(b, RCS_OP_LINE, ln);
    rcs_fence fe = {7}; ADD(b, RCS_OP_FENCE, fe);
    rcs_present pr = {0, frame_no}; ADD(b, RCS_OP_PRESENT, pr);
}

/* ---- tests ------------------------------------------------------------------- */

static void test_happy_path_and_determinism(void) {
    uint64_t hashes[2];
    for (int k = 0; k < 2; ++k) {
        rcs_client c; rcs_client_init(&c, RCS_R_ALL);
        CHECK_OK(make_surfaces(&c));
        /* upload a 64x64 sprite with 50% alpha */
        static uint32_t sprite[64 * 64];
        for (int i = 0; i < 64 * 64; ++i) sprite[i] = 0x80FF0000u | (uint32_t)(i & 0xFF);
        bb b; bb_begin(&b, RCS_BATCH_RESOURCE);
        rcs_upload up = {1, {0, 0, 64, 64}}; bb_add_raw(&b, RCS_OP_UPLOAD, &up, sizeof up, sprite, sizeof sprite);
        rcs_result r = submit(&c, &b); CHECK_OK(r); CHECK(r.upload_bytes == sizeof sprite);
        draw_scene(&b, 1); r = submit(&c, &b); CHECK_OK(r);
        CHECK(r.pixel_ops == (uint64_t)W * H + 100 * 40 + 64 * 64 * 2 + 1280);
        CHECK(r.fences_done == 1);
        CHECK(c.presents == 1 && !c.in_frame);
        CHECK(c.surfaces[0].px[15 * W + 15] == 0xFFFFAA00u);           /* fill landed */
        CHECK(c.surfaces[0].px[5 * W + 5] == 0xFF202020u);              /* clear landed */
        CHECK(c.surfaces[0].px[(H - 1) * W + (W - 1)] == 0xFFFFFFFFu);  /* line endpoint */
        hashes[k] = rcs_surface_hash(&c, 0);
        rcs_client_free(&c);
    }
    CHECK(hashes[0] == hashes[1] && hashes[0] != 0);
}

static void test_blend_rules(void) {
    /* ALPHA: exact-round (t + (t >> 8)) >> 8 with t = s*a + d*(255-a) + 128, dst alpha unchanged. ADD: saturating. */
    rcs_client c; rcs_client_init(&c, RCS_R_ALL);
    bb b; bb_begin(&b, RCS_BATCH_RESOURCE);
    rcs_create_surface s0 = {0, 4, 1, 0}, s1 = {1, 4, 1, 0}; ADD(&b, RCS_OP_CREATE_SURFACE, s0); ADD(&b, RCS_OP_CREATE_SURFACE, s1);
    CHECK_OK(submit(&c, &b));
    uint32_t dst[4] = {0x40102030u, 0xFF808080u, 0x00FFFFFFu, 0x80F0F0F0u};
    uint32_t src[4] = {0x80FF0000u, 0x00FFFFFFu, 0xFF123456u, 0x7F101010u};
    rcs_upload u0 = {0, {0, 0, 4, 1}}, u1 = {1, {0, 0, 4, 1}};
    bb_begin(&b, RCS_BATCH_RESOURCE); bb_add_raw(&b, RCS_OP_UPLOAD, &u0, sizeof u0, dst, sizeof dst); bb_add_raw(&b, RCS_OP_UPLOAD, &u1, sizeof u1, src, sizeof src); CHECK_OK(submit(&c, &b));
    rcs_begin_frame bf = {1}; rcs_blit bl = {0, 0, 0, 1, {0, 0, 4, 1}, RCS_BLEND_ALPHA, 0};
    bb_begin(&b, RCS_BATCH_FRAME); ADD(&b, RCS_OP_BEGIN_FRAME, bf); ADD(&b, RCS_OP_BLIT, bl); CHECK_OK(submit(&c, &b));
    for (int i = 0; i < 4; ++i) {
        uint32_t a = src[i] >> 24, ia = 255 - a, want = dst[i] & 0xFF000000u;
        for (int ch = 0; ch < 3; ++ch) {
            uint32_t sv = (src[i] >> (8 * ch)) & 0xFF, dv = (dst[i] >> (8 * ch)) & 0xFF;
            uint32_t t = sv * a + dv * ia + 128u; want |= ((t + (t >> 8)) >> 8) << (8 * ch);
        }
        CHECK(c.surfaces[0].px[i] == want);
    }
    CHECK(c.surfaces[0].px[1] == 0xFF808080u);  /* zero source alpha leaves the destination untouched */
    CHECK(c.surfaces[0].px[2] == 0x00123456u);  /* full source alpha copies colour, keeps dst alpha */
    bl.blend = RCS_BLEND_ADD;
    bb_begin(&b, RCS_BATCH_FRAME); ADD(&b, RCS_OP_BLIT, bl); CHECK_OK(submit(&c, &b));
    CHECK(c.surfaces[0].px[2] == 0xFF2468ACu);  /* 0x00123456 + 0xFF123456 */
    CHECK(c.surfaces[0].px[3] == 0xFF909090u);  /* alpha blit gave 0x80808080 colour; ADD of 0x7F101010 saturates alpha to 0xFF */
    rcs_client_free(&c);
}

static void test_damage(void) {
    rcs_client c; rcs_client_init(&c, RCS_R_ALL); CHECK_OK(make_surfaces(&c));
    CHECK(c.surfaces[0].tw == 80 && c.surfaces[0].th == 45);
    bb b; rcs_begin_frame bf = {1}; rcs_present pr = {0, 1};
    rcs_fill_rect fr = {0, {10, 10, 100, 40}, 0xFFFFAA00u};     /* tiles x 0..6, y 0..3 = 28 */
    bb_begin(&b, RCS_BATCH_FRAME); ADD(&b, RCS_OP_BEGIN_FRAME, bf); ADD(&b, RCS_OP_FILL_RECT, fr); ADD(&b, RCS_OP_PRESENT, pr); CHECK_OK(submit(&c, &b));
    CHECK(c.last_present.dirty_tiles == 28 && c.last_present.slot == 0 && c.last_present.frame_no == 1);
    CHECK(c.last_present.tiles[0] & 1);                           /* tile (0,0) */
    CHECK((c.last_present.tiles[(6) >> 3] >> (6 & 7)) & 1);        /* tile (6,0) */
    CHECK(!((c.last_present.tiles[(7) >> 3] >> (7 & 7)) & 1));     /* tile (7,0) untouched */
    /* a static frame reports zero damage; a full clear reports every tile */
    bf.frame_no = 2; pr.frame_no = 2;
    bb_begin(&b, RCS_BATCH_FRAME); ADD(&b, RCS_OP_BEGIN_FRAME, bf); ADD(&b, RCS_OP_PRESENT, pr); CHECK_OK(submit(&c, &b));
    CHECK(c.last_present.dirty_tiles == 0);
    bf.frame_no = 3; pr.frame_no = 3; rcs_clear cl = {0, 0};
    bb_begin(&b, RCS_BATCH_FRAME); ADD(&b, RCS_OP_BEGIN_FRAME, bf); ADD(&b, RCS_OP_CLEAR, cl); ADD(&b, RCS_OP_PRESENT, pr); CHECK_OK(submit(&c, &b));
    CHECK(c.last_present.dirty_tiles == 80 * 45);
    /* a line across the frame marks one tile per step at most, and only inside the clip */
    bf.frame_no = 4; pr.frame_no = 4; rcs_set_clip sc = {0, {0, 0, 64, 64}}; rcs_line ln = {0, 0, 0, 1279, 719, 1};
    bb_begin(&b, RCS_BATCH_FRAME); ADD(&b, RCS_OP_BEGIN_FRAME, bf); ADD(&b, RCS_OP_SET_CLIP, sc); ADD(&b, RCS_OP_LINE, ln); ADD(&b, RCS_OP_PRESENT, pr); CHECK_OK(submit(&c, &b));
    CHECK(c.last_present.dirty_tiles > 0 && c.last_present.dirty_tiles <= 16);
    /* a rejected batch leaves no damage behind */
    bf.frame_no = 5; pr.frame_no = 5; rcs_fill_rect bad = {0, {0, 0, 0, 5}, 0};
    bb_begin(&b, RCS_BATCH_FRAME); ADD(&b, RCS_OP_BEGIN_FRAME, bf); ADD(&b, RCS_OP_CLEAR, cl); ADD(&b, RCS_OP_FILL_RECT, bad); CHECK_REJ(submit(&c, &b), 2, RCS_E_RECT_EMPTY);
    bb_begin(&b, RCS_BATCH_FRAME); ADD(&b, RCS_OP_BEGIN_FRAME, bf); ADD(&b, RCS_OP_PRESENT, pr); CHECK_OK(submit(&c, &b));
    CHECK(c.last_present.dirty_tiles == 0);
    rcs_client_free(&c);
}

static void test_envelope(void) {
    rcs_client c; rcs_client_init(&c, RCS_R_ALL); make_surfaces(&c);
    bb b;
    /* too large */
    draw_scene(&b, 1); bb_finish(&b);
    CHECK_REJ(rcs_submit(&c, b.buf, RCS_MAX_BATCH_BYTES + 1), 0xFFFFFFFFu, RCS_E_TOO_LARGE);
    /* shorter than a header */
    CHECK_REJ(rcs_submit(&c, b.buf, 8), 0xFFFFFFFFu, RCS_E_LENGTH_MISMATCH);
    /* bad magic / version / kind / reserved */
    rcs_batch_hdr h; memcpy(&h, b.buf, sizeof h);
    rcs_batch_hdr x = h; x.magic ^= 1; memcpy(b.buf, &x, sizeof x); CHECK_REJ(rcs_submit(&c, b.buf, b.len), 0xFFFFFFFFu, RCS_E_BAD_MAGIC);
    x = h; x.version = 2; memcpy(b.buf, &x, sizeof x); CHECK_REJ(rcs_submit(&c, b.buf, b.len), 0xFFFFFFFFu, RCS_E_BAD_VERSION);
    x = h; x.kind = 3; memcpy(b.buf, &x, sizeof x); CHECK_REJ(rcs_submit(&c, b.buf, b.len), 0xFFFFFFFFu, RCS_E_BAD_KIND);
    x = h; x.reserved = 1; memcpy(b.buf, &x, sizeof x); CHECK_REJ(rcs_submit(&c, b.buf, b.len), 0xFFFFFFFFu, RCS_E_RESERVED_NONZERO);
    /* total_bytes disagrees with what the ring delivered (sender-controlled length is ignored) */
    x = h; x.total_bytes += 4; memcpy(b.buf, &x, sizeof x); CHECK_REJ(rcs_submit(&c, b.buf, b.len), 0xFFFFFFFFu, RCS_E_LENGTH_MISMATCH);
    memcpy(b.buf, &h, sizeof h);
    /* trailing bytes */
    x = h; x.total_bytes += 3; memcpy(b.buf, &x, sizeof x); CHECK_REJ(rcs_submit(&c, b.buf, b.len + 3), 0xFFFFFFFFu, RCS_E_LENGTH_MISMATCH);
    memcpy(b.buf, &h, sizeof h);
    /* truncated last body */
    x = h; x.total_bytes -= 2; memcpy(b.buf, &x, sizeof x); CHECK_REJ(rcs_submit(&c, b.buf, b.len - 2), 6, RCS_E_LENGTH_MISMATCH);
    memcpy(b.buf, &h, sizeof h);
    /* too many commands */
    x = h; x.cmd_count = RCS_MAX_BATCH_CMDS + 1; memcpy(b.buf, &x, sizeof x); CHECK_REJ(rcs_submit(&c, b.buf, b.len), 0xFFFFFFFFu, RCS_E_TOO_MANY_CMDS);
    /* cmd_count larger than the bytes hold */
    x = h; x.cmd_count = 8; memcpy(b.buf, &x, sizeof x); CHECK_REJ(rcs_submit(&c, b.buf, b.len), 7, RCS_E_LENGTH_MISMATCH);
    memcpy(b.buf, &h, sizeof h);
    /* bad seq, bad opcode, bad body_len, flags, cmd version */
    rcs_cmd_hdr ch; size_t off = sizeof h + sizeof ch + sizeof(rcs_begin_frame);  /* second command: CLEAR */
    memcpy(&ch, b.buf + off, sizeof ch);
    rcs_cmd_hdr y = ch; y.seq = 5; memcpy(b.buf + off, &y, sizeof y); CHECK_REJ(rcs_submit(&c, b.buf, b.len), 1, RCS_E_BAD_SEQ);
    y = ch; y.opcode = 0x99; memcpy(b.buf + off, &y, sizeof y); CHECK_REJ(rcs_submit(&c, b.buf, b.len), 1, RCS_E_BAD_OPCODE);
    y = ch; y.body_len = 12; memcpy(b.buf + off, &y, sizeof y); CHECK_REJ(rcs_submit(&c, b.buf, b.len), 1, RCS_E_BAD_BODY_LEN);
    y = ch; y.flags = 1; memcpy(b.buf + off, &y, sizeof y); CHECK_REJ(rcs_submit(&c, b.buf, b.len), 1, RCS_E_RESERVED_NONZERO);
    y = ch; y.version = 0; memcpy(b.buf + off, &y, sizeof y); CHECK_REJ(rcs_submit(&c, b.buf, b.len), 1, RCS_E_BAD_VERSION);
    /* a resource op inside a frame batch, and the reverse */
    y = ch; y.opcode = RCS_OP_CLEAR; memcpy(b.buf + off, &y, sizeof y);
    bb r; bb_begin(&r, RCS_BATCH_FRAME); rcs_create_surface cs = {9, 8, 8, 0}; ADD(&r, RCS_OP_CREATE_SURFACE, cs);
    CHECK_REJ(submit(&c, &r), 0, RCS_E_WRONG_CLASS);
    bb_begin(&r, RCS_BATCH_RESOURCE); rcs_begin_frame bf = {1}; ADD(&r, RCS_OP_BEGIN_FRAME, bf);
    CHECK_REJ(submit(&c, &r), 0, RCS_E_WRONG_CLASS);
    CHECK(!c.in_frame && c.presents == 0);   /* nothing above changed state */
    rcs_client_free(&c);
}

static void test_rights_and_atomicity(void) {
    rcs_client c; rcs_client_init(&c, RCS_R_ALL & ~RCS_R_MESH); make_surfaces(&c);
    bb b; draw_scene(&b, 1); CHECK_OK(submit(&c, &b));
    uint64_t before = rcs_surface_hash(&c, 0);

    /* missing right: rejected at the exact command, nothing else applied */
    bb_begin(&b, RCS_BATCH_FRAME);
    rcs_begin_frame bf = {2}; ADD(&b, RCS_OP_BEGIN_FRAME, bf);
    rcs_clear cl = {0, 0}; ADD(&b, RCS_OP_CLEAR, cl);
    rcs_draw_triangles dt = {0, 0, 0, 1, RCS_SLOT_NONE, 0}; ADD(&b, RCS_OP_DRAW_TRIANGLES, dt);
    CHECK_REJ(submit(&c, &b), 2, RCS_E_RIGHT_MISSING);
    CHECK(rcs_surface_hash(&c, 0) == before);
    CHECK(!c.in_frame);                       /* the BEGIN_FRAME in the rejected batch did not take effect */

    /* promise-style drop: later commands in the same batch already lack the right */
    bb_begin(&b, RCS_BATCH_FRAME);
    ADD(&b, RCS_OP_BEGIN_FRAME, bf);
    rcs_promise_drop pd = {RCS_R_DRAW}; ADD(&b, RCS_OP_PROMISE_DROP, pd);
    ADD(&b, RCS_OP_CLEAR, cl);
    CHECK_REJ(submit(&c, &b), 2, RCS_E_RIGHT_MISSING);
    CHECK(c.rights == (RCS_R_ALL & ~RCS_R_MESH));  /* rejected batch: the drop did not happen either */

    /* a committed drop is one-way */
    bb_begin(&b, RCS_BATCH_FRAME); ADD(&b, RCS_OP_PROMISE_DROP, pd); CHECK_OK(submit(&c, &b));
    CHECK(c.rights == (RCS_R_ALL & ~RCS_R_MESH & ~RCS_R_DRAW));
    bb_begin(&b, RCS_BATCH_FRAME); ADD(&b, RCS_OP_BEGIN_FRAME, bf); ADD(&b, RCS_OP_CLEAR, cl);
    CHECK_REJ(submit(&c, &b), 1, RCS_E_RIGHT_MISSING);
    rcs_promise_drop bad = {0x100}; bb_begin(&b, RCS_BATCH_FRAME); ADD(&b, RCS_OP_PROMISE_DROP, bad);
    CHECK_REJ(submit(&c, &b), 0, RCS_E_RESERVED_NONZERO);

    /* a client with no rights at all can still do nothing */
    rcs_client z; rcs_client_init(&z, 0);
    bb_begin(&b, RCS_BATCH_RESOURCE); rcs_create_surface cs = {0, 8, 8, 0}; ADD(&b, RCS_OP_CREATE_SURFACE, cs);
    CHECK_REJ(submit(&z, &b), 0, RCS_E_RIGHT_MISSING);
    rcs_client_free(&z); rcs_client_free(&c);
}

static void test_slots_and_resources(void) {
    rcs_client c; rcs_client_init(&c, RCS_R_ALL); make_surfaces(&c);
    bb b;
    bb_begin(&b, RCS_BATCH_RESOURCE); rcs_create_surface cs = {RCS_MAX_SURFACES, 8, 8, 0}; ADD(&b, RCS_OP_CREATE_SURFACE, cs);
    CHECK_REJ(submit(&c, &b), 0, RCS_E_SLOT_RANGE);
    bb_begin(&b, RCS_BATCH_RESOURCE); cs.slot = 0; ADD(&b, RCS_OP_CREATE_SURFACE, cs);
    CHECK_REJ(submit(&c, &b), 0, RCS_E_SLOT_OCCUPIED);
    bb_begin(&b, RCS_BATCH_RESOURCE); cs.slot = 5; cs.w = 0; ADD(&b, RCS_OP_CREATE_SURFACE, cs);
    CHECK_REJ(submit(&c, &b), 0, RCS_E_BAD_DIM);
    bb_begin(&b, RCS_BATCH_RESOURCE); cs.w = RCS_MAX_DIM + 1; cs.h = 1; ADD(&b, RCS_OP_CREATE_SURFACE, cs);
    CHECK_REJ(submit(&c, &b), 0, RCS_E_BAD_DIM);
    bb_begin(&b, RCS_BATCH_RESOURCE); cs.w = 8; cs.h = 8; cs.format = 7; ADD(&b, RCS_OP_CREATE_SURFACE, cs);
    CHECK_REJ(submit(&c, &b), 0, RCS_E_BAD_FORMAT);
    /* pixel budget across the client's surfaces: 2 x 4096x4096 = 32M > 8M */
    bb_begin(&b, RCS_BATCH_RESOURCE); cs.format = 0; cs.w = cs.h = RCS_MAX_DIM; ADD(&b, RCS_OP_CREATE_SURFACE, cs);
    CHECK_REJ(submit(&c, &b), 0, RCS_E_SURFACE_BUDGET);
    /* destroy then re-create in one batch is legal (validated sequentially on the shadow) */
    bb_begin(&b, RCS_BATCH_RESOURCE); rcs_destroy_surface ds = {1}; ADD(&b, RCS_OP_DESTROY_SURFACE, ds);
    cs.slot = 1; cs.w = cs.h = 32; ADD(&b, RCS_OP_CREATE_SURFACE, cs); CHECK_OK(submit(&c, &b));
    CHECK(c.surfaces[1].w == 32);
    /* destroy an empty slot, and a slot destroyed earlier in the same batch */
    bb_begin(&b, RCS_BATCH_RESOURCE); ds.slot = 200; ADD(&b, RCS_OP_DESTROY_SURFACE, ds);
    CHECK_REJ(submit(&c, &b), 0, RCS_E_SLOT_EMPTY);
    bb_begin(&b, RCS_BATCH_RESOURCE); ds.slot = 2; ADD(&b, RCS_OP_DESTROY_SURFACE, ds); ADD(&b, RCS_OP_DESTROY_SURFACE, ds);
    CHECK_REJ(submit(&c, &b), 1, RCS_E_SLOT_EMPTY);
    CHECK(c.surfaces[2].live);   /* whole batch rolled back */

    /* assets: the table is filled by policy/storage, never by the client */
    static uint32_t img[16 * 16];
    c.assets[3] = (rcs_asset){ RCS_ASSET_IMAGE, 16, 16, RCS_FMT_ARGB8888, 0, img, NULL };
    static int32_t tri[7] = { 0, 0, 10 << 16, 0, 0, 10 << 16, (int32_t)0xFF00FF00 };
    c.assets[4] = (rcs_asset){ RCS_ASSET_MESH, 0, 0, 0, 1, NULL, tri };
    bb_begin(&b, RCS_BATCH_RESOURCE); cs.slot = 6; cs.w = cs.h = 16; ADD(&b, RCS_OP_CREATE_SURFACE, cs); CHECK_OK(submit(&c, &b));
    rcs_bind_asset ba = {3, 6};
    bb_begin(&b, RCS_BATCH_RESOURCE); ADD(&b, RCS_OP_BIND_ASSET, ba); CHECK_OK(submit(&c, &b));
    ba.asset_slot = 4; bb_begin(&b, RCS_BATCH_RESOURCE); ADD(&b, RCS_OP_BIND_ASSET, ba); CHECK_REJ(submit(&c, &b), 0, RCS_E_ASSET_KIND);
    ba.asset_slot = 9; bb_begin(&b, RCS_BATCH_RESOURCE); ADD(&b, RCS_OP_BIND_ASSET, ba); CHECK_REJ(submit(&c, &b), 0, RCS_E_SLOT_EMPTY);
    ba.asset_slot = RCS_MAX_ASSETS; bb_begin(&b, RCS_BATCH_RESOURCE); ADD(&b, RCS_OP_BIND_ASSET, ba); CHECK_REJ(submit(&c, &b), 0, RCS_E_SLOT_RANGE);
    ba.asset_slot = 3; ba.surface_slot = 1; bb_begin(&b, RCS_BATCH_RESOURCE); ADD(&b, RCS_OP_BIND_ASSET, ba); CHECK_REJ(submit(&c, &b), 0, RCS_E_BAD_DIM);
    /* mesh: range and kind */
    rcs_begin_frame bf = {1};
    rcs_draw_triangles dt = {0, 4, 0, 1, RCS_SLOT_NONE, RCS_SHADE_FLAT};
    bb_begin(&b, RCS_BATCH_FRAME); ADD(&b, RCS_OP_BEGIN_FRAME, bf); ADD(&b, RCS_OP_DRAW_TRIANGLES, dt); CHECK_OK(submit(&c, &b));
    CHECK(c.surfaces[0].px[2 * W + 2] == 0xFF00FF00u);
    dt.first_tri = 1; bb_begin(&b, RCS_BATCH_FRAME); ADD(&b, RCS_OP_DRAW_TRIANGLES, dt); CHECK_REJ(submit(&c, &b), 0, RCS_E_MESH_RANGE);
    dt.first_tri = 0xFFFFFFFFu; dt.tri_count = 2; bb_begin(&b, RCS_BATCH_FRAME); ADD(&b, RCS_OP_DRAW_TRIANGLES, dt); CHECK_REJ(submit(&c, &b), 0, RCS_E_MESH_RANGE); /* no u32 wrap */
    dt.first_tri = 0; dt.tri_count = 0; bb_begin(&b, RCS_BATCH_FRAME); ADD(&b, RCS_OP_DRAW_TRIANGLES, dt); CHECK_REJ(submit(&c, &b), 0, RCS_E_MESH_RANGE);
    dt.tri_count = 1; dt.mesh_slot = 3; bb_begin(&b, RCS_BATCH_FRAME); ADD(&b, RCS_OP_DRAW_TRIANGLES, dt); CHECK_REJ(submit(&c, &b), 0, RCS_E_ASSET_KIND);
    dt.mesh_slot = 4; dt.shading = 9; bb_begin(&b, RCS_BATCH_FRAME); ADD(&b, RCS_OP_DRAW_TRIANGLES, dt); CHECK_REJ(submit(&c, &b), 0, RCS_E_BAD_SHADING);
    dt.shading = 0; dt.texture = 0; bb_begin(&b, RCS_BATCH_FRAME); ADD(&b, RCS_OP_DRAW_TRIANGLES, dt); CHECK_REJ(submit(&c, &b), 0, RCS_E_SAME_SURFACE);
    dt.texture = 2; bb_begin(&b, RCS_BATCH_FRAME); ADD(&b, RCS_OP_DRAW_TRIANGLES, dt); CHECK_REJ(submit(&c, &b), 0, RCS_E_FORMAT_MISMATCH); /* A8 texture */
    dt.target = 2; dt.texture = RCS_SLOT_NONE; bb_begin(&b, RCS_BATCH_FRAME); ADD(&b, RCS_OP_DRAW_TRIANGLES, dt); CHECK_REJ(submit(&c, &b), 0, RCS_E_FORMAT_MISMATCH); /* A8 target */
    rcs_client_free(&c);
}

static void test_geometry(void) {
    rcs_client c; rcs_client_init(&c, RCS_R_ALL); make_surfaces(&c);
    bb b; rcs_begin_frame bf = {1};
    bb_begin(&b, RCS_BATCH_FRAME); ADD(&b, RCS_OP_BEGIN_FRAME, bf); CHECK_OK(submit(&c, &b));

    /* FILL_RECT at the far corners of int32: clipped, cheap, no overflow */
    rcs_fill_rect fr = {0, {INT32_MAX, INT32_MAX, INT32_MAX, INT32_MAX}, 1};
    bb_begin(&b, RCS_BATCH_FRAME); ADD(&b, RCS_OP_FILL_RECT, fr); rcs_result r = submit(&c, &b); CHECK_OK(r); CHECK(r.pixel_ops == 0);
    fr.r = (rcs_rect){INT32_MIN, INT32_MIN, INT32_MAX, INT32_MAX};
    bb_begin(&b, RCS_BATCH_FRAME); ADD(&b, RCS_OP_FILL_RECT, fr); r = submit(&c, &b); CHECK_OK(r); CHECK(r.pixel_ops == 0);
    fr.r = (rcs_rect){-50, -50, 100, 100};   /* partly inside: 50x50 charged */
    bb_begin(&b, RCS_BATCH_FRAME); ADD(&b, RCS_OP_FILL_RECT, fr); r = submit(&c, &b); CHECK_OK(r); CHECK(r.pixel_ops == 2500);
    fr.r = (rcs_rect){0, 0, 0, 10};
    bb_begin(&b, RCS_BATCH_FRAME); ADD(&b, RCS_OP_FILL_RECT, fr); CHECK_REJ(submit(&c, &b), 0, RCS_E_RECT_EMPTY);
    fr.r = (rcs_rect){0, 0, -5, 10};
    bb_begin(&b, RCS_BATCH_FRAME); ADD(&b, RCS_OP_FILL_RECT, fr); CHECK_REJ(submit(&c, &b), 0, RCS_E_RECT_EMPTY);

    /* BLIT: source rect must lie inside the source; destination is clipped */
    rcs_blit bl = {0, 0, 0, 1, {0, 0, 64, 64}, RCS_BLEND_COPY, 0};
    bl.sr = (rcs_rect){1, 0, 64, 64}; bb_begin(&b, RCS_BATCH_FRAME); ADD(&b, RCS_OP_BLIT, bl); CHECK_REJ(submit(&c, &b), 0, RCS_E_RECT_OUTSIDE);
    bl.sr = (rcs_rect){INT32_MAX, 0, 1, 1}; bb_begin(&b, RCS_BATCH_FRAME); ADD(&b, RCS_OP_BLIT, bl); CHECK_REJ(submit(&c, &b), 0, RCS_E_RECT_OUTSIDE);
    bl.sr = (rcs_rect){0, 0, 64, 64}; bl.dx = INT32_MAX; bb_begin(&b, RCS_BATCH_FRAME); ADD(&b, RCS_OP_BLIT, bl); r = submit(&c, &b); CHECK_OK(r); CHECK(r.pixel_ops == 0);
    bl.dx = -32; bl.dy = -32; bb_begin(&b, RCS_BATCH_FRAME); ADD(&b, RCS_OP_BLIT, bl); r = submit(&c, &b); CHECK_OK(r); CHECK(r.pixel_ops == 32 * 32);
    bl.dx = bl.dy = 0; bl.src = 0; bb_begin(&b, RCS_BATCH_FRAME); ADD(&b, RCS_OP_BLIT, bl); CHECK_REJ(submit(&c, &b), 0, RCS_E_SAME_SURFACE);
    bl.src = 2; bb_begin(&b, RCS_BATCH_FRAME); ADD(&b, RCS_OP_BLIT, bl); CHECK_REJ(submit(&c, &b), 0, RCS_E_FORMAT_MISMATCH);
    bl.src = 1; bl.blend = 3; bb_begin(&b, RCS_BATCH_FRAME); ADD(&b, RCS_OP_BLIT, bl); CHECK_REJ(submit(&c, &b), 0, RCS_E_BAD_BLEND);
    bl.blend = 0; bl.flags = 1; bb_begin(&b, RCS_BATCH_FRAME); ADD(&b, RCS_OP_BLIT, bl); CHECK_REJ(submit(&c, &b), 0, RCS_E_RESERVED_NONZERO);
    bl.flags = 0; bl.src = 7; bb_begin(&b, RCS_BATCH_FRAME); ADD(&b, RCS_OP_BLIT, bl); CHECK_REJ(submit(&c, &b), 0, RCS_E_SLOT_EMPTY);
    bl.target = RCS_MAX_SURFACES; bb_begin(&b, RCS_BATCH_FRAME); ADD(&b, RCS_OP_BLIT, bl); CHECK_REJ(submit(&c, &b), 0, RCS_E_SLOT_RANGE);
    /* A8 to A8 copy is fine, A8 alpha blend is not */
    bb_begin(&b, RCS_BATCH_RESOURCE); rcs_create_surface a8 = {3, 64, 64, RCS_FMT_A8}; ADD(&b, RCS_OP_CREATE_SURFACE, a8);
    bb_finish(&b); CHECK_OK(rcs_submit(&c, b.buf, b.len));
    bl = (rcs_blit){3, 0, 0, 2, {0, 0, 64, 64}, RCS_BLEND_COPY, 0};
    bb_begin(&b, RCS_BATCH_FRAME); ADD(&b, RCS_OP_BLIT, bl); CHECK_OK(submit(&c, &b));
    bl.blend = RCS_BLEND_ALPHA; bb_begin(&b, RCS_BATCH_FRAME); ADD(&b, RCS_OP_BLIT, bl); CHECK_REJ(submit(&c, &b), 0, RCS_E_BAD_BLEND);

    /* SET_CLIP must lie inside; draws are then clipped to it */
    rcs_set_clip sc = {0, {100, 100, 10, 10}};
    bb_begin(&b, RCS_BATCH_FRAME); ADD(&b, RCS_OP_SET_CLIP, sc);
    rcs_clear cl = {0, 0xFF0000FFu}; ADD(&b, RCS_OP_CLEAR, cl);
    r = submit(&c, &b); CHECK_OK(r); CHECK(r.pixel_ops == 100);
    CHECK(c.surfaces[0].px[105 * W + 105] == 0xFF0000FFu && c.surfaces[0].px[99 * W + 99] != 0xFF0000FFu);
    sc.r = (rcs_rect){W - 5, 0, 10, 10}; bb_begin(&b, RCS_BATCH_FRAME); ADD(&b, RCS_OP_SET_CLIP, sc); CHECK_REJ(submit(&c, &b), 0, RCS_E_RECT_OUTSIDE);
    sc.r = (rcs_rect){0, 0, 0, 10}; bb_begin(&b, RCS_BATCH_FRAME); ADD(&b, RCS_OP_SET_CLIP, sc); CHECK_REJ(submit(&c, &b), 0, RCS_E_RECT_EMPTY);

    /* LINE with endpoints far outside: cost is the span, so it is a budget question, not a crash */
    rcs_line ln = {0, -10, -10, 50, 50, 0xFFFFFFFFu};
    bb_begin(&b, RCS_BATCH_FRAME); ADD(&b, RCS_OP_LINE, ln); r = submit(&c, &b); CHECK_OK(r); CHECK(r.pixel_ops == 61);
    ln = (rcs_line){0, INT32_MIN, INT32_MIN, INT32_MAX, INT32_MAX, 1};
    bb_begin(&b, RCS_BATCH_FRAME); ADD(&b, RCS_OP_LINE, ln); CHECK_REJ(submit(&c, &b), 0, RCS_E_PIXEL_BUDGET);

    /* UPLOAD: rect inside, payload length exactly w*h*bpp, cap 64 KiB */
    static uint8_t payload[RCS_MAX_UPLOAD_BYTES + 64];
    rcs_upload up = {1, {0, 0, 64, 64}};
    bb_begin(&b, RCS_BATCH_RESOURCE); bb_add_raw(&b, RCS_OP_UPLOAD, &up, sizeof up, payload, 64 * 64 * 4 - 4); CHECK_REJ(submit(&c, &b), 0, RCS_E_BAD_BODY_LEN);
    bb_begin(&b, RCS_BATCH_RESOURCE); bb_add_raw(&b, RCS_OP_UPLOAD, &up, sizeof up, payload, 64 * 64 * 4 + 4); CHECK_REJ(submit(&c, &b), 0, RCS_E_BAD_BODY_LEN);
    bb_begin(&b, RCS_BATCH_RESOURCE); bb_add_raw(&b, RCS_OP_UPLOAD, &up, sizeof up, payload, 64 * 64 * 4); CHECK_OK(submit(&c, &b));
    up.r = (rcs_rect){INT32_MAX, 0, 1, 1}; bb_begin(&b, RCS_BATCH_RESOURCE); bb_add_raw(&b, RCS_OP_UPLOAD, &up, sizeof up, payload, 4); CHECK_REJ(submit(&c, &b), 0, RCS_E_RECT_OUTSIDE);
    up.r = (rcs_rect){-1, 0, 1, 1}; bb_begin(&b, RCS_BATCH_RESOURCE); bb_add_raw(&b, RCS_OP_UPLOAD, &up, sizeof up, payload, 4); CHECK_REJ(submit(&c, &b), 0, RCS_E_RECT_OUTSIDE);
    up = (rcs_upload){0, {0, 0, 256, 65}};  /* 66,560 bytes > 64 KiB */
    bb_begin(&b, RCS_BATCH_RESOURCE); bb_add_raw(&b, RCS_OP_UPLOAD, &up, sizeof up, payload, 0);
    /* body_len header says fixed+0 here; make it claim the real size to reach the cap check */
    rcs_cmd_hdr ch; memcpy(&ch, b.buf + sizeof(rcs_batch_hdr), sizeof ch); ch.body_len = sizeof up + 256 * 65 * 4; memcpy(b.buf + sizeof(rcs_batch_hdr), &ch, sizeof ch);
    CHECK_REJ(submit(&c, &b), 0, RCS_E_BAD_BODY_LEN);
    up.r = (rcs_rect){0, 0, 2, 2}; up.slot = 2;  /* A8: 1 byte per pixel */
    bb_begin(&b, RCS_BATCH_RESOURCE); bb_add_raw(&b, RCS_OP_UPLOAD, &up, sizeof up, payload, 4); CHECK_OK(submit(&c, &b));
    bb_begin(&b, RCS_BATCH_RESOURCE); bb_add_raw(&b, RCS_OP_UPLOAD, &up, sizeof up, payload, 16); CHECK_REJ(submit(&c, &b), 0, RCS_E_BAD_BODY_LEN);
    rcs_client_free(&c);
}

static void test_frame_state_and_budgets(void) {
    rcs_client c; rcs_client_init(&c, RCS_R_ALL); make_surfaces(&c);
    bb b; rcs_begin_frame bf = {1}; rcs_clear cl = {0, 0}; rcs_present pr = {0, 1};
    bb_begin(&b, RCS_BATCH_FRAME); ADD(&b, RCS_OP_CLEAR, cl); CHECK_REJ(submit(&c, &b), 0, RCS_E_NOT_IN_FRAME);
    bb_begin(&b, RCS_BATCH_FRAME); ADD(&b, RCS_OP_PRESENT, pr); CHECK_REJ(submit(&c, &b), 0, RCS_E_NOT_IN_FRAME);
    bb_begin(&b, RCS_BATCH_FRAME); ADD(&b, RCS_OP_BEGIN_FRAME, bf); ADD(&b, RCS_OP_BEGIN_FRAME, bf); CHECK_REJ(submit(&c, &b), 1, RCS_E_ALREADY_IN_FRAME);
    pr.frame_no = 2; bb_begin(&b, RCS_BATCH_FRAME); ADD(&b, RCS_OP_BEGIN_FRAME, bf); ADD(&b, RCS_OP_PRESENT, pr); CHECK_REJ(submit(&c, &b), 1, RCS_E_FRAME_NO);
    pr.frame_no = 1; pr.target = 2; bb_begin(&b, RCS_BATCH_FRAME); ADD(&b, RCS_OP_BEGIN_FRAME, bf); ADD(&b, RCS_OP_PRESENT, pr); CHECK_REJ(submit(&c, &b), 1, RCS_E_FORMAT_MISMATCH);
    pr.target = 0; bb_begin(&b, RCS_BATCH_FRAME); ADD(&b, RCS_OP_BEGIN_FRAME, bf); ADD(&b, RCS_OP_PRESENT, pr); CHECK_OK(submit(&c, &b));
    /* frame numbers must increase across presents; a frame may span several batches */
    bb_begin(&b, RCS_BATCH_FRAME); ADD(&b, RCS_OP_BEGIN_FRAME, bf); CHECK_REJ(submit(&c, &b), 0, RCS_E_FRAME_NO);
    bf.frame_no = 2; bb_begin(&b, RCS_BATCH_FRAME); ADD(&b, RCS_OP_BEGIN_FRAME, bf); CHECK_OK(submit(&c, &b));
    bb_begin(&b, RCS_BATCH_FRAME); ADD(&b, RCS_OP_CLEAR, cl); CHECK_OK(submit(&c, &b));
    pr.frame_no = 2; bb_begin(&b, RCS_BATCH_FRAME); ADD(&b, RCS_OP_PRESENT, pr); CHECK_OK(submit(&c, &b));

    /* pixel budget: 6,000,000 / 921,600 per 720p clear = 6 full clears, the 7th is over */
    bf.frame_no = 3; bb_begin(&b, RCS_BATCH_FRAME); ADD(&b, RCS_OP_BEGIN_FRAME, bf);
    for (int i = 0; i < 6; ++i) ADD(&b, RCS_OP_CLEAR, cl);
    rcs_result r = submit(&c, &b); CHECK_OK(r); CHECK(r.pixel_ops == 6ull * W * H);
    bb_begin(&b, RCS_BATCH_FRAME); ADD(&b, RCS_OP_CLEAR, cl); CHECK_REJ(submit(&c, &b), 0, RCS_E_PIXEL_BUDGET);
    rcs_fill_rect fr = {0, {0, 0, 100, 100}, 0}; bb_begin(&b, RCS_BATCH_FRAME); ADD(&b, RCS_OP_FILL_RECT, fr); CHECK_OK(submit(&c, &b)); /* 10,000 still fit */
    /* the budget resets with the next frame */
    pr.frame_no = 3; bb_begin(&b, RCS_BATCH_FRAME); ADD(&b, RCS_OP_PRESENT, pr); CHECK_OK(submit(&c, &b));
    bf.frame_no = 4; bb_begin(&b, RCS_BATCH_FRAME); ADD(&b, RCS_OP_BEGIN_FRAME, bf); ADD(&b, RCS_OP_CLEAR, cl); CHECK_OK(submit(&c, &b));

    /* triangle budget */
    c.assets[4] = (rcs_asset){ RCS_ASSET_MESH, 0, 0, 0, 100000, NULL, NULL };
    rcs_draw_triangles dt = {0, 4, 0, RCS_FRAME_TRIANGLE_BUDGET, RCS_SLOT_NONE, 0};
    bb_begin(&b, RCS_BATCH_FRAME); ADD(&b, RCS_OP_DRAW_TRIANGLES, dt); r = submit(&c, &b); CHECK_OK(r); CHECK(r.triangles == RCS_FRAME_TRIANGLE_BUDGET);
    dt.tri_count = 1; bb_begin(&b, RCS_BATCH_FRAME); ADD(&b, RCS_OP_DRAW_TRIANGLES, dt); CHECK_REJ(submit(&c, &b), 0, RCS_E_TRIANGLE_BUDGET);

    /* upload budget: 2 MiB per frame = 32 uploads of 64 KiB */
    static uint8_t payload[RCS_MAX_UPLOAD_BYTES];
    rcs_upload up = {0, {0, 0, 128, 128}};  /* 64 KiB */
    bb_begin(&b, RCS_BATCH_RESOURCE);
    for (int i = 0; i < 5; ++i) bb_add_raw(&b, RCS_OP_UPLOAD, &up, sizeof up, payload, sizeof payload);
    /* 5 x (16 + 20 + 65536) > 256 KiB batch cap, so this is TOO_LARGE: the batch cap bounds a batch, the frame cap bounds a frame */
    CHECK_REJ(submit(&c, &b), 0xFFFFFFFFu, RCS_E_TOO_LARGE);
    for (int i = 0; i < 32; ++i) { bb_begin(&b, RCS_BATCH_RESOURCE); bb_add_raw(&b, RCS_OP_UPLOAD, &up, sizeof up, payload, sizeof payload); CHECK_OK(submit(&c, &b)); }
    bb_begin(&b, RCS_BATCH_RESOURCE); bb_add_raw(&b, RCS_OP_UPLOAD, &up, sizeof up, payload, sizeof payload); CHECK_REJ(submit(&c, &b), 0, RCS_E_UPLOAD_BUDGET);

    /* bind budget: 8 MiB of asset bytes per frame = two 720p backgrounds; the third is over */
    c.assets[5] = (rcs_asset){ RCS_ASSET_IMAGE, W, H, RCS_FMT_ARGB8888, 0, NULL, NULL };
    rcs_bind_asset ba = {5, 0};
    bb_begin(&b, RCS_BATCH_RESOURCE); ADD(&b, RCS_OP_BIND_ASSET, ba); ADD(&b, RCS_OP_BIND_ASSET, ba); r = submit(&c, &b); CHECK_OK(r); CHECK(r.bind_bytes == 2 * W * H * 4);
    bb_begin(&b, RCS_BATCH_RESOURCE); ADD(&b, RCS_OP_BIND_ASSET, ba); CHECK_REJ(submit(&c, &b), 0, RCS_E_BIND_BUDGET);

    /* fence budget */
    rcs_fence fe = {1};
    bb_begin(&b, RCS_BATCH_FRAME); for (uint32_t i = 0; i <= RCS_MAX_FENCES_PER_BATCH; ++i) ADD(&b, RCS_OP_FENCE, fe);
    CHECK_REJ(submit(&c, &b), RCS_MAX_FENCES_PER_BATCH, RCS_E_FENCE_BUDGET);
    rcs_client_free(&c);
}

/* ---- fuzz ------------------------------------------------------------------------ */
static uint64_t rng_state = 0x9E3779B97F4A7C15ull;
static uint64_t rnd(void) { uint64_t x = rng_state; x ^= x << 13; x ^= x >> 7; x ^= x << 17; rng_state = x; return x; }

static void test_fuzz(void) {
    rcs_client c; rcs_client_init(&c, RCS_R_ALL);
    bb b; bb_begin(&b, RCS_BATCH_RESOURCE);
    rcs_create_surface s0 = {0, 96, 64, 0}, s1 = {1, 32, 32, 0}, s2 = {2, 32, 32, 1};
    ADD(&b, RCS_OP_CREATE_SURFACE, s0); ADD(&b, RCS_OP_CREATE_SURFACE, s1); ADD(&b, RCS_OP_CREATE_SURFACE, s2);
    CHECK_OK(submit(&c, &b));
    static int32_t mesh[64 * 7];
    for (int i = 0; i < 64 * 7; ++i) mesh[i] = (int32_t)(rnd() % (200u << 16)) - (100 << 16);
    c.assets[0] = (rcs_asset){ RCS_ASSET_MESH, 0, 0, 0, 64, NULL, mesh };
    static uint32_t img[32 * 32];
    c.assets[1] = (rcs_asset){ RCS_ASSET_IMAGE, 32, 32, 0, 0, img, NULL };

    uint32_t accepted = 0, rejected = 0, errs[RCS_E_COUNT] = {0};
    const uint16_t ops[] = { RCS_OP_CREATE_SURFACE, RCS_OP_DESTROY_SURFACE, RCS_OP_BIND_ASSET, RCS_OP_UPLOAD, RCS_OP_SET_CLIP,
        RCS_OP_CLEAR, RCS_OP_FILL_RECT, RCS_OP_BLIT, RCS_OP_LINE, RCS_OP_DRAW_TRIANGLES, RCS_OP_BEGIN_FRAME, RCS_OP_PRESENT,
        RCS_OP_FENCE, RCS_OP_PROMISE_DROP, 0x00, 0x0F, 0x30, 0xFFFF };

    /* (a) random bytes: must never crash or be accepted as a non-trivial batch */
    for (int it = 0; it < 20000; ++it) {
        size_t n = rnd() % 512;
        for (size_t i = 0; i < n; ++i) b.buf[i] = (uint8_t)rnd();
        if (rnd() & 1) { rcs_batch_hdr h = { RCS_BATCH_MAGIC, RCS_VERSION, (uint8_t)(1 + (rnd() & 1)), 0, (uint32_t)(rnd() % 8), (uint32_t)n }; if (n >= sizeof h) memcpy(b.buf, &h, sizeof h); }
        rcs_result r = rcs_submit(&c, b.buf, n);
        if (r.status == RCS_OK) { accepted++; CHECK(r.pixel_ops == 0 || c.in_frame || c.have_presented); } else { rejected++; errs[r.error < RCS_E_COUNT ? r.error : 0]++; }
    }
    /* (b) structured: well-formed envelopes with random command bodies, so validation logic is what gets exercised */
    for (int it = 0; it < 60000; ++it) {
        uint8_t kind = (rnd() & 1) ? RCS_BATCH_FRAME : RCS_BATCH_RESOURCE;
        bb_begin(&b, kind);
        uint32_t k = 1 + (uint32_t)(rnd() % 6);
        for (uint32_t j = 0; j < k; ++j) {
            uint16_t op = ops[rnd() % (sizeof ops / sizeof ops[0])];
            if (rnd() % 5) {   /* mostly pick an op of the batch's own class so deeper checks are reached */
                bool resource = op >= 0x01 && op <= 0x0F;
                if (kind == RCS_BATCH_RESOURCE && !resource) op = ops[rnd() % 4];
                if (kind == RCS_BATCH_FRAME && resource) op = ops[4 + rnd() % 10];
            }
            uint8_t body[64]; uint32_t len = rcs_body_size(op); if (!len) len = (uint32_t)(rnd() % 48);
            for (uint32_t i = 0; i < len; ++i) body[i] = (uint8_t)rnd();
            /* steer slots and small ints into range often enough to reach deep checks */
            if (len >= 4 && (rnd() % 4)) { uint32_t v = (uint32_t)(rnd() % 4); memcpy(body, &v, 4); }
            if (len >= 24 && (rnd() % 2)) { int32_t r4[4] = {(int32_t)(rnd() % 100) - 20, (int32_t)(rnd() % 100) - 20, (int32_t)(rnd() % 80), (int32_t)(rnd() % 80)}; memcpy(body + 4, r4, 16); }
            uint8_t pay[256]; uint32_t plen = 0;
            if (op == RCS_OP_UPLOAD) { int32_t r4[4] = {(int32_t)(rnd() % 40), (int32_t)(rnd() % 40), 1 + (int32_t)(rnd() % 4), 1 + (int32_t)(rnd() % 4)}; memcpy(body + 4, r4, 16); plen = (uint32_t)(rnd() % 80); for (uint32_t i = 0; i < plen; ++i) pay[i] = (uint8_t)rnd(); }
            if (op == RCS_OP_PROMISE_DROP) { uint32_t m = (rnd() % 8 == 0) ? (uint32_t)rnd() : 0; memcpy(body, &m, 4); } /* mostly drop nothing, else fuzz runs out of rights */
            if (op == RCS_OP_BEGIN_FRAME) { uint32_t f = c.last_presented + 1 + (uint32_t)(rnd() % 2); memcpy(body, &f, 4); }
            if (op == RCS_OP_PRESENT) { uint32_t t = 0; memcpy(body, &t, 4); uint32_t f = c.frame_no; memcpy(body + 4, &f, 4); }
            if (op == RCS_OP_DRAW_TRIANGLES) { uint32_t v[6] = {0, 0, (uint32_t)(rnd() % 70), 1 + (uint32_t)(rnd() % 8), RCS_SLOT_NONE, (uint32_t)(rnd() % 3)}; memcpy(body, v, 24); }
            if (op == RCS_OP_BIND_ASSET) { uint32_t v[2] = {(uint32_t)(rnd() % 3), (uint32_t)(rnd() % 4)}; memcpy(body, v, 8); }
            if (op == RCS_OP_CREATE_SURFACE) { uint32_t v[4] = {(uint32_t)(rnd() % 5), 1 + (uint32_t)(rnd() % 40), 1 + (uint32_t)(rnd() % 40), (uint32_t)(rnd() % 3)}; memcpy(body, v, 16); }
            bb_add_raw(&b, op, body, len, pay, plen);
        }
        if (rnd() % 16 == 0) b.buf[sizeof(rcs_batch_hdr) + rnd() % 8] ^= (uint8_t)(1u << (rnd() % 8)); /* occasional header corruption */
        if (rnd() % 64 == 0) { rcs_cmd_hdr ch; memcpy(&ch, b.buf + sizeof(rcs_batch_hdr), sizeof ch); ch.seq = (uint32_t)rnd(); memcpy(b.buf + sizeof(rcs_batch_hdr), &ch, sizeof ch); }
        if (rnd() % 64 == 0) { b.buf[5] = (uint8_t)rnd(); }                                /* batch kind */
        if (rnd() % 64 == 0) { uint32_t n = (uint32_t)rnd(); memcpy(b.buf + 8, &n, 4); bb_finish(&b); memcpy(b.buf + 8, &n, 4); } /* cmd_count */
        rcs_result r = (rnd() % 64 == 0 && b.len >= sizeof(rcs_batch_hdr)) ? rcs_submit(&c, b.buf, b.len) : submit(&c, &b);  /* sometimes send with a stale header */
        if (it % 200 == 0) c.rights = RCS_R_ALL;   /* harness: PROMISE_DROP would otherwise starve the fuzz of rights */
        if (r.status == RCS_OK) accepted++; else { rejected++; errs[r.error < RCS_E_COUNT ? r.error : 0]++; CHECK(r.failing_index < k || r.failing_index == 0xFFFFFFFFu); }
        /* surfaces 0..2 must survive for the fuzz to stay meaningful: re-create them if destroyed */
        if (!c.surfaces[0].live || !c.surfaces[1].live || !c.surfaces[2].live) {
            if (c.in_frame) { bb_begin(&b, RCS_BATCH_FRAME); rcs_present pr = {0, c.frame_no}; if (c.surfaces[0].live) { ADD(&b, RCS_OP_PRESENT, pr); submit(&c, &b); } }
            bb_begin(&b, RCS_BATCH_RESOURCE);
            if (!c.surfaces[0].live) ADD(&b, RCS_OP_CREATE_SURFACE, s0);
            if (!c.surfaces[1].live) ADD(&b, RCS_OP_CREATE_SURFACE, s1);
            if (!c.surfaces[2].live) ADD(&b, RCS_OP_CREATE_SURFACE, s2);
            c.rights = RCS_R_ALL; /* test harness, not the protocol: restore rights so the fuzz keeps reaching draws */
            if (c.in_frame) c.in_frame = false;
            submit(&c, &b);
        }
    }
    printf("fuzz: accepted=%u rejected=%u\n", accepted, rejected);
    printf("fuzz: rejection reasons:");
    for (uint32_t e = 1; e < RCS_E_COUNT; ++e) if (errs[e]) printf(" %s=%u", rcs_error_name(e), errs[e]);
    printf("\n");
    CHECK(accepted > 1000);          /* the structured fuzz reaches the executor */
    uint32_t distinct = 0; for (uint32_t e = 1; e < RCS_E_COUNT; ++e) if (errs[e]) distinct++;
    CHECK(distinct >= 25);           /* and exercises most validation rules */
    rcs_client_free(&c);
}

int main(void) {
    test_happy_path_and_determinism();
    test_blend_rules();
    test_damage();
    test_envelope();
    test_rights_and_atomicity();
    test_slots_and_resources();
    test_geometry();
    test_frame_state_and_budgets();
    test_fuzz();
    printf("rcs_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
