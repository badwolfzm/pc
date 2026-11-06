/*
 * rcs.c - reference validator and executor for the Warden render service
 * command set. See rcs.h for the contract and gpu-commands.md for the spec.
 *
 * Structure of rcs_submit():
 *   1. envelope check on the receiver's private copy of the bytes
 *   2. validation pass over every command against a COPY of the client's
 *      bookkeeping state (slots, budgets, frame state), mutating only the copy
 *   3. if every command passed, the execution pass runs the same commands and
 *      the copy becomes the client's state; otherwise nothing changes
 *
 * The executor trusts nothing: it recomputes clipping from validated values
 * and asserts the invariants the validator guarantees. Under -fsanitize this
 * turns any gap between validator and executor into a test failure.
 */
#include "rcs.h"
#include <string.h>
#include <stdlib.h>
#include <assert.h>

/* Optional SIMD backend: build with -DRCS_USE_SUPEROPS and link matrixos/superops
 * to run FILL_RECT, CLEAR, BLIT and DRAW_TRIANGLES on the mxs_* kernels (scalar,
 * 128-bit SSE2/NEON or AVX2 tier). Validation, damage and accounting are unchanged;
 * the kernels are bit-identical to the scalar loops below (rcs_test passes either way).
 * A kernel error here is a validator/executor gap and aborts. */
#ifdef RCS_USE_SUPEROPS
#include "../../superops/mx_superops.h"
static mxs_surface mxs_view(rcs_surface *t) {
    mxs_surface v = { mxs_span_of(t->px, (uint64_t)t->w * t->h), t->w, t->h, t->w };
    return v;
}
static void mxs_must(mxs_err e) { if (e != MXS_OK) abort(); }
#endif

/* ---- helpers ------------------------------------------------------------ */

static uint32_t bpp_of(uint32_t fmt) { return fmt == RCS_FMT_A8 ? 1u : 4u; }

static bool rect_inside(const rcs_rect *r, uint32_t w, uint32_t h) {
    if (r->w <= 0 || r->h <= 0 || r->x < 0 || r->y < 0) return false;
    return (int64_t)r->x + r->w <= (int64_t)w && (int64_t)r->y + r->h <= (int64_t)h;
}

static bool intersect(const rcs_rect *a, const rcs_rect *b, rcs_rect *out) {
    int64_t x0 = a->x > b->x ? a->x : b->x;
    int64_t y0 = a->y > b->y ? a->y : b->y;
    int64_t ax1 = (int64_t)a->x + a->w, bx1 = (int64_t)b->x + b->w;
    int64_t ay1 = (int64_t)a->y + a->h, by1 = (int64_t)b->y + b->h;
    int64_t x1 = ax1 < bx1 ? ax1 : bx1, y1 = ay1 < by1 ? ay1 : by1;
    if (x1 <= x0 || y1 <= y0) return false;
    out->x = (int32_t)x0; out->y = (int32_t)y0; out->w = (int32_t)(x1 - x0); out->h = (int32_t)(y1 - y0);
    return true;
}

uint32_t rcs_body_size(uint16_t op) {
    switch (op) {
    case RCS_OP_CREATE_SURFACE:  return sizeof(rcs_create_surface);
    case RCS_OP_DESTROY_SURFACE: return sizeof(rcs_destroy_surface);
    case RCS_OP_BIND_ASSET:      return sizeof(rcs_bind_asset);
    case RCS_OP_UPLOAD:          return sizeof(rcs_upload);
    case RCS_OP_SET_CLIP:        return sizeof(rcs_set_clip);
    case RCS_OP_CLEAR:           return sizeof(rcs_clear);
    case RCS_OP_FILL_RECT:       return sizeof(rcs_fill_rect);
    case RCS_OP_BLIT:            return sizeof(rcs_blit);
    case RCS_OP_LINE:            return sizeof(rcs_line);
    case RCS_OP_DRAW_TRIANGLES:  return sizeof(rcs_draw_triangles);
    case RCS_OP_BEGIN_FRAME:     return sizeof(rcs_begin_frame);
    case RCS_OP_PRESENT:         return sizeof(rcs_present);
    case RCS_OP_FENCE:           return sizeof(rcs_fence);
    case RCS_OP_PROMISE_DROP:    return sizeof(rcs_promise_drop);
    default: return 0;
    }
}

static uint32_t right_of(uint16_t op) {
    switch (op) {
    case RCS_OP_CREATE_SURFACE: case RCS_OP_DESTROY_SURFACE: return RCS_R_RESOURCE;
    case RCS_OP_BIND_ASSET: return RCS_R_ASSET;
    case RCS_OP_UPLOAD: return RCS_R_UPLOAD;
    case RCS_OP_SET_CLIP: case RCS_OP_CLEAR: case RCS_OP_FILL_RECT: case RCS_OP_BLIT: case RCS_OP_LINE: return RCS_R_DRAW;
    case RCS_OP_DRAW_TRIANGLES: return RCS_R_MESH;
    case RCS_OP_BEGIN_FRAME: case RCS_OP_PRESENT: case RCS_OP_FENCE: return RCS_R_PRESENT;
    case RCS_OP_PROMISE_DROP: return 0;
    default: return 0xFFFFFFFFu;
    }
}

static bool class_ok(uint16_t op, uint8_t kind) {
    bool resource = op >= 0x01 && op <= 0x0F;
    return kind == RCS_BATCH_RESOURCE ? resource : !resource;
}

const char *rcs_error_name(uint32_t e) {
    static const char *names[RCS_E_COUNT] = {
        "none", "too_large", "bad_magic", "bad_version", "bad_kind", "reserved_nonzero",
        "length_mismatch", "too_many_cmds", "bad_seq", "bad_opcode", "wrong_class", "bad_body_len",
        "right_missing", "not_in_frame", "already_in_frame", "frame_no",
        "slot_range", "slot_empty", "slot_occupied", "asset_kind", "same_surface",
        "bad_dim", "bad_format", "format_mismatch", "rect_empty", "rect_outside", "bad_blend",
        "bad_shading", "mesh_range", "surface_budget", "upload_budget", "pixel_budget",
        "triangle_budget", "fence_budget", "bind_budget" };
    return e < RCS_E_COUNT ? names[e] : "?";
}

void rcs_client_init(rcs_client *c, uint32_t rights) {
    memset(c, 0, sizeof(*c));
    c->rights = rights & RCS_R_ALL;
}

void rcs_client_free(rcs_client *c) {
    for (uint32_t i = 0; i < RCS_MAX_SURFACES; ++i) { free(c->surfaces[i].px); c->surfaces[i].px = NULL; free(c->surfaces[i].tiles); c->surfaces[i].tiles = NULL; c->surfaces[i].live = false; }
}

uint64_t rcs_surface_hash(const rcs_client *c, uint32_t slot) {
    uint64_t h = 1469598103934665603ULL;
    if (slot >= RCS_MAX_SURFACES || !c->surfaces[slot].live) return 0;
    const rcs_surface *s = &c->surfaces[slot];
    size_t n = (size_t)s->w * s->h;
    for (size_t i = 0; i < n; ++i) {
        uint32_t p = s->px[i];
        for (int b = 0; b < 4; ++b) { h ^= (p >> (8 * b)) & 0xFFu; h *= 1099511628211ULL; }
    }
    return h;
}

/* ---- shadow state used by the validation pass ---------------------------- */

typedef struct {
    bool     live[RCS_MAX_SURFACES];
    uint32_t w[RCS_MAX_SURFACES], h[RCS_MAX_SURFACES], fmt[RCS_MAX_SURFACES];
    uint64_t surface_pixels;
    uint32_t rights;
    bool     in_frame;
    uint32_t frame_no;
    bool     have_presented;
    uint32_t last_presented;
    uint64_t pixel_ops;
    uint32_t triangles;
    uint32_t upload;
    uint32_t bind;
    bool     clip_set;
    uint32_t clip_target;
    rcs_rect clip;
    uint32_t fences;
    uint64_t batch_pixel_ops;
    uint32_t batch_triangles, batch_upload, batch_bind;
} shadow;

static void shadow_from(shadow *s, const rcs_client *c) {
    memset(s, 0, sizeof(*s));
    for (uint32_t i = 0; i < RCS_MAX_SURFACES; ++i) {
        s->live[i] = c->surfaces[i].live; s->w[i] = c->surfaces[i].w; s->h[i] = c->surfaces[i].h; s->fmt[i] = c->surfaces[i].format;
    }
    s->surface_pixels = c->surface_pixels; s->rights = c->rights; s->in_frame = c->in_frame; s->frame_no = c->frame_no;
    s->have_presented = c->have_presented; s->last_presented = c->last_presented;
    s->pixel_ops = c->frame_pixel_ops; s->triangles = c->frame_triangles; s->upload = c->frame_upload; s->bind = c->frame_bind;
    s->clip_set = c->clip_set; s->clip_target = c->clip_target; s->clip = c->clip;
}

/* The drawable region for a target: the clip if one is set on it, else the whole surface. */
static rcs_rect region_of(const shadow *s, uint32_t t) {
    rcs_rect full = {0, 0, (int32_t)s->w[t], (int32_t)s->h[t]};
    if (s->clip_set && s->clip_target == t) return s->clip;
    return full;
}

static uint32_t need_surface(const shadow *s, uint32_t slot) {
    if (slot >= RCS_MAX_SURFACES) return RCS_E_SLOT_RANGE;
    if (!s->live[slot]) return RCS_E_SLOT_EMPTY;
    return RCS_E_NONE;
}

static uint32_t charge_pixels(shadow *s, uint64_t ops) {
    if (s->pixel_ops + ops > RCS_FRAME_PIXEL_BUDGET) return RCS_E_PIXEL_BUDGET;
    s->pixel_ops += ops; s->batch_pixel_ops += ops;
    return RCS_E_NONE;
}

/* Cost of a BLIT/FILL over a region after clipping: area, doubled when blending reads the destination. */
static uint64_t area_cost(const rcs_rect *r, bool reads_dst) {
    uint64_t a = (uint64_t)r->w * (uint64_t)r->h;
    return reads_dst ? a * 2u : a;
}

/* Validate one command against the shadow. Returns an error code. */
static uint32_t validate_cmd(shadow *s, const rcs_client *c, uint16_t op, const uint8_t *body, uint32_t body_len) {
    uint32_t need = right_of(op);
    if (need != 0 && !(s->rights & need)) return RCS_E_RIGHT_MISSING;

    switch (op) {
    case RCS_OP_CREATE_SURFACE: {
        rcs_create_surface a; memcpy(&a, body, sizeof a);
        if (a.slot >= RCS_MAX_SURFACES) return RCS_E_SLOT_RANGE;
        if (s->live[a.slot]) return RCS_E_SLOT_OCCUPIED;
        if (a.w == 0 || a.h == 0 || a.w > RCS_MAX_DIM || a.h > RCS_MAX_DIM) return RCS_E_BAD_DIM;
        if (a.format >= RCS_FMT_COUNT) return RCS_E_BAD_FORMAT;
        uint64_t px = (uint64_t)a.w * a.h;
        if (s->surface_pixels + px > RCS_MAX_SURFACE_PIXELS) return RCS_E_SURFACE_BUDGET;
        s->live[a.slot] = true; s->w[a.slot] = a.w; s->h[a.slot] = a.h; s->fmt[a.slot] = a.format; s->surface_pixels += px;
        return RCS_E_NONE;
    }
    case RCS_OP_DESTROY_SURFACE: {
        rcs_destroy_surface a; memcpy(&a, body, sizeof a);
        uint32_t e = need_surface(s, a.slot); if (e) return e;
        s->surface_pixels -= (uint64_t)s->w[a.slot] * s->h[a.slot];
        s->live[a.slot] = false;
        if (s->clip_set && s->clip_target == a.slot) s->clip_set = false;
        return RCS_E_NONE;
    }
    case RCS_OP_BIND_ASSET: {
        rcs_bind_asset a; memcpy(&a, body, sizeof a);
        if (a.asset_slot >= RCS_MAX_ASSETS) return RCS_E_SLOT_RANGE;
        const rcs_asset *as = &c->assets[a.asset_slot];
        if (as->kind == RCS_ASSET_NONE) return RCS_E_SLOT_EMPTY;
        if (as->kind != RCS_ASSET_IMAGE) return RCS_E_ASSET_KIND;
        uint32_t e = need_surface(s, a.surface_slot); if (e) return e;
        if (s->w[a.surface_slot] != as->w || s->h[a.surface_slot] != as->h) return RCS_E_BAD_DIM;
        if (s->fmt[a.surface_slot] != as->format) return RCS_E_FORMAT_MISMATCH;
        uint64_t bytes = (uint64_t)as->w * as->h * bpp_of(as->format);
        if (s->bind + bytes > RCS_FRAME_BIND_BUDGET) return RCS_E_BIND_BUDGET;
        s->bind += (uint32_t)bytes; s->batch_bind += (uint32_t)bytes;
        return RCS_E_NONE;
    }
    case RCS_OP_UPLOAD: {
        rcs_upload a; memcpy(&a, body, sizeof a);
        uint32_t e = need_surface(s, a.slot); if (e) return e;
        if (a.r.w <= 0 || a.r.h <= 0) return RCS_E_RECT_EMPTY;
        if (!rect_inside(&a.r, s->w[a.slot], s->h[a.slot])) return RCS_E_RECT_OUTSIDE;
        uint64_t bytes = (uint64_t)a.r.w * (uint64_t)a.r.h * bpp_of(s->fmt[a.slot]);
        if (bytes > RCS_MAX_UPLOAD_BYTES) return RCS_E_BAD_BODY_LEN;
        if (body_len != sizeof a + bytes) return RCS_E_BAD_BODY_LEN;
        if (s->upload + (uint32_t)bytes > RCS_FRAME_UPLOAD_BUDGET) return RCS_E_UPLOAD_BUDGET;
        s->upload += (uint32_t)bytes; s->batch_upload += (uint32_t)bytes;
        return RCS_E_NONE;
    }
    case RCS_OP_SET_CLIP: {
        rcs_set_clip a; memcpy(&a, body, sizeof a);
        if (!s->in_frame) return RCS_E_NOT_IN_FRAME;
        uint32_t e = need_surface(s, a.target); if (e) return e;
        if (a.r.w <= 0 || a.r.h <= 0) return RCS_E_RECT_EMPTY;
        if (!rect_inside(&a.r, s->w[a.target], s->h[a.target])) return RCS_E_RECT_OUTSIDE;
        s->clip_set = true; s->clip_target = a.target; s->clip = a.r;
        return RCS_E_NONE;
    }
    case RCS_OP_CLEAR: {
        rcs_clear a; memcpy(&a, body, sizeof a);
        if (!s->in_frame) return RCS_E_NOT_IN_FRAME;
        uint32_t e = need_surface(s, a.target); if (e) return e;
        rcs_rect reg = region_of(s, a.target);
        return charge_pixels(s, area_cost(&reg, false));
    }
    case RCS_OP_FILL_RECT: {
        rcs_fill_rect a; memcpy(&a, body, sizeof a);
        if (!s->in_frame) return RCS_E_NOT_IN_FRAME;
        uint32_t e = need_surface(s, a.target); if (e) return e;
        if (a.r.w <= 0 || a.r.h <= 0) return RCS_E_RECT_EMPTY;
        rcs_rect reg = region_of(s, a.target), d;
        if (!intersect(&a.r, &reg, &d)) return RCS_E_NONE;   /* fully clipped: legal, free */
        return charge_pixels(s, area_cost(&d, false));
    }
    case RCS_OP_BLIT: {
        rcs_blit a; memcpy(&a, body, sizeof a);
        if (!s->in_frame) return RCS_E_NOT_IN_FRAME;
        uint32_t e = need_surface(s, a.target); if (e) return e;
        e = need_surface(s, a.src); if (e) return e;
        if (a.src == a.target) return RCS_E_SAME_SURFACE;
        if (a.blend >= RCS_BLEND_COUNT) return RCS_E_BAD_BLEND;
        if (a.flags != 0) return RCS_E_RESERVED_NONZERO;
        if (a.sr.w <= 0 || a.sr.h <= 0) return RCS_E_RECT_EMPTY;
        if (!rect_inside(&a.sr, s->w[a.src], s->h[a.src])) return RCS_E_RECT_OUTSIDE;
        if (s->fmt[a.src] != s->fmt[a.target]) return RCS_E_FORMAT_MISMATCH;
        if (a.blend != RCS_BLEND_COPY && s->fmt[a.src] != RCS_FMT_ARGB8888) return RCS_E_BAD_BLEND;
        rcs_rect dst = {a.dx, a.dy, a.sr.w, a.sr.h}, reg = region_of(s, a.target), d;
        if (!intersect(&dst, &reg, &d)) return RCS_E_NONE;
        return charge_pixels(s, area_cost(&d, a.blend != RCS_BLEND_COPY));
    }
    case RCS_OP_LINE: {
        rcs_line a; memcpy(&a, body, sizeof a);
        if (!s->in_frame) return RCS_E_NOT_IN_FRAME;
        uint32_t e = need_surface(s, a.target); if (e) return e;
        int64_t dx = (int64_t)a.x1 - a.x0, dy = (int64_t)a.y1 - a.y0;
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        /* Endpoints may lie outside; the cost is the full span, so a huge span is simply over budget. */
        uint64_t steps = (uint64_t)(dx > dy ? dx : dy) + 1u;
        return charge_pixels(s, steps);
    }
    case RCS_OP_DRAW_TRIANGLES: {
        rcs_draw_triangles a; memcpy(&a, body, sizeof a);
        if (!s->in_frame) return RCS_E_NOT_IN_FRAME;
        uint32_t e = need_surface(s, a.target); if (e) return e;
        if (s->fmt[a.target] != RCS_FMT_ARGB8888) return RCS_E_FORMAT_MISMATCH;
        if (a.mesh_slot >= RCS_MAX_ASSETS) return RCS_E_SLOT_RANGE;
        const rcs_asset *m = &c->assets[a.mesh_slot];
        if (m->kind == RCS_ASSET_NONE) return RCS_E_SLOT_EMPTY;
        if (m->kind != RCS_ASSET_MESH) return RCS_E_ASSET_KIND;
        if (a.tri_count == 0) return RCS_E_MESH_RANGE;
        if ((uint64_t)a.first_tri + a.tri_count > m->tri_count) return RCS_E_MESH_RANGE;
        if (a.shading >= RCS_SHADE_COUNT) return RCS_E_BAD_SHADING;
        if (a.texture != RCS_SLOT_NONE) {
            e = need_surface(s, a.texture); if (e) return e;
            if (a.texture == a.target) return RCS_E_SAME_SURFACE;
            if (s->fmt[a.texture] != RCS_FMT_ARGB8888) return RCS_E_FORMAT_MISMATCH;
        }
        if (s->triangles + a.tri_count > RCS_FRAME_TRIANGLE_BUDGET) return RCS_E_TRIANGLE_BUDGET;
        s->triangles += a.tri_count; s->batch_triangles += a.tri_count;
        return RCS_E_NONE;
    }
    case RCS_OP_BEGIN_FRAME: {
        rcs_begin_frame a; memcpy(&a, body, sizeof a);
        if (s->in_frame) return RCS_E_ALREADY_IN_FRAME;
        if (s->have_presented && a.frame_no <= s->last_presented) return RCS_E_FRAME_NO;
        s->in_frame = true; s->frame_no = a.frame_no;
        s->pixel_ops = 0; s->triangles = 0; s->upload = 0; s->bind = 0; s->clip_set = false;
        return RCS_E_NONE;
    }
    case RCS_OP_PRESENT: {
        rcs_present a; memcpy(&a, body, sizeof a);
        if (!s->in_frame) return RCS_E_NOT_IN_FRAME;
        if (a.frame_no != s->frame_no) return RCS_E_FRAME_NO;
        uint32_t e = need_surface(s, a.target); if (e) return e;
        if (s->fmt[a.target] != RCS_FMT_ARGB8888) return RCS_E_FORMAT_MISMATCH;
        s->in_frame = false; s->have_presented = true; s->last_presented = a.frame_no; s->clip_set = false;
        return RCS_E_NONE;
    }
    case RCS_OP_FENCE: {
        if (s->fences >= RCS_MAX_FENCES_PER_BATCH) return RCS_E_FENCE_BUDGET;
        s->fences++;
        return RCS_E_NONE;
    }
    case RCS_OP_PROMISE_DROP: {
        rcs_promise_drop a; memcpy(&a, body, sizeof a);
        if (a.rights_mask & ~RCS_R_ALL) return RCS_E_RESERVED_NONZERO;
        s->rights &= ~a.rights_mask;   /* one-way; later commands in this batch see the narrower set */
        return RCS_E_NONE;
    }
    default:
        return RCS_E_BAD_OPCODE;
    }
}

/* ---- executor ------------------------------------------------------------ */

/* Blend rules (normative; the SIMD super-op kernels must match bit for bit):
 *   COPY : out = src
 *   ALPHA: per colour channel, a = src alpha: t = s*a + d*(255-a) + 128; out = (t + (t >> 8)) >> 8
 *          (exact rounding of /255). Destination alpha is left unchanged.
 *   ADD  : per channel saturating add on all four bytes.
 * This is the dirty-bit canvas thread's rounding rule, adopted here so that the
 * compositor, the super-ops and this reference agree. */
static inline uint32_t div255_round(uint32_t t) { t += 128u; return (t + (t >> 8)) >> 8; }

static inline uint32_t blend_px(uint32_t d, uint32_t sp, uint32_t mode) {
    if (mode == RCS_BLEND_COPY) return sp;
    uint32_t sa = sp >> 24, da = d >> 24;
    uint32_t sr = (sp >> 16) & 0xFF, sg = (sp >> 8) & 0xFF, sb = sp & 0xFF;
    uint32_t dr = (d >> 16) & 0xFF, dg = (d >> 8) & 0xFF, db = d & 0xFF;
    if (mode == RCS_BLEND_ALPHA) {
        uint32_t ia = 255u - sa;
        uint32_t r = div255_round(sr * sa + dr * ia), g = div255_round(sg * sa + dg * ia), b = div255_round(sb * sa + db * ia);
        return (da << 24) | (r << 16) | (g << 8) | b;
    }
    /* ADD, saturating */
    uint32_t r = sr + dr, g = sg + dg, b = sb + db, a = sa + da;
    if (r > 255u) r = 255u;
    if (g > 255u) g = 255u;
    if (b > 255u) b = 255u;
    if (a > 255u) a = 255u;
    return (a << 24) | (r << 16) | (g << 8) | b;
}

/* Damage: mark the tiles a validated, clipped rect touches. */
static void damage_rect(rcs_surface *t, const rcs_rect *d) {
    assert(rect_inside(d, t->w, t->h));
    uint32_t tx0 = (uint32_t)d->x / RCS_TILE, ty0 = (uint32_t)d->y / RCS_TILE;
    uint32_t tx1 = ((uint32_t)d->x + (uint32_t)d->w - 1) / RCS_TILE, ty1 = ((uint32_t)d->y + (uint32_t)d->h - 1) / RCS_TILE;
    for (uint32_t ty = ty0; ty <= ty1; ++ty)
        for (uint32_t tx = tx0; tx <= tx1; ++tx) {
            size_t bit = (size_t)ty * t->tw + tx;
            uint8_t m = (uint8_t)(1u << (bit & 7));
            if (!(t->tiles[bit >> 3] & m)) { t->tiles[bit >> 3] |= m; t->dirty++; }
        }
}
static void damage_px(rcs_surface *t, int64_t x, int64_t y) {
    rcs_rect r = {(int32_t)x, (int32_t)y, 1, 1};
    damage_rect(t, &r);
}
static void damage_clear(rcs_surface *t) {
    memset(t->tiles, 0, ((size_t)t->tw * t->th + 7) / 8);
    t->dirty = 0;
}

static void fill(rcs_surface *t, const rcs_rect *d, uint32_t color) {
    assert(rect_inside(d, t->w, t->h));
    damage_rect(t, d);
#ifdef RCS_USE_SUPEROPS
    { mxs_surface v = mxs_view(t); mxs_rect r = {d->x, d->y, d->w, d->h}; mxs_must(mxs_fill_rect(&v, &r, color)); return; }
#endif
    for (int32_t j = 0; j < d->h; ++j) {
        uint32_t *row = t->px + (size_t)(d->y + j) * t->w + (size_t)d->x;
        for (int32_t i = 0; i < d->w; ++i) row[i] = color;
    }
}

static void exec_cmd(rcs_client *c, uint16_t op, const uint8_t *body) {
    switch (op) {
    case RCS_OP_CREATE_SURFACE: {
        rcs_create_surface a; memcpy(&a, body, sizeof a);
        rcs_surface *s = &c->surfaces[a.slot];
        assert(!s->live);
        s->px = (uint32_t *)calloc((size_t)a.w * a.h, sizeof(uint32_t));
        if (!s->px) abort();  /* a real service charges the client's memory budget; here it is fatal */
        s->tw = (a.w + RCS_TILE - 1) / RCS_TILE; s->th = (a.h + RCS_TILE - 1) / RCS_TILE;
        s->tiles = (uint8_t *)calloc(((size_t)s->tw * s->th + 7) / 8, 1);
        if (!s->tiles) abort();
        s->dirty = 0;
        s->live = true; s->w = a.w; s->h = a.h; s->format = a.format;
        c->surface_pixels += (uint64_t)a.w * a.h;
        break;
    }
    case RCS_OP_DESTROY_SURFACE: {
        rcs_destroy_surface a; memcpy(&a, body, sizeof a);
        rcs_surface *s = &c->surfaces[a.slot];
        assert(s->live);
        c->surface_pixels -= (uint64_t)s->w * s->h;
        free(s->px); s->px = NULL; free(s->tiles); s->tiles = NULL; s->live = false; s->w = s->h = 0;
        if (c->clip_set && c->clip_target == a.slot) c->clip_set = false;
        break;
    }
    case RCS_OP_BIND_ASSET: {
        rcs_bind_asset a; memcpy(&a, body, sizeof a);
        rcs_surface *s = &c->surfaces[a.surface_slot];
        const rcs_asset *as = &c->assets[a.asset_slot];
        assert(s->live && as->kind == RCS_ASSET_IMAGE && s->w == as->w && s->h == as->h);
        { rcs_rect all = {0, 0, (int32_t)s->w, (int32_t)s->h}; damage_rect(s, &all); }
        if (as->image) memcpy(s->px, as->image, (size_t)s->w * s->h * sizeof(uint32_t));
        else memset(s->px, 0, (size_t)s->w * s->h * sizeof(uint32_t));   /* asset not yet streamed in: stand-in */
        c->frame_bind += (uint32_t)((uint64_t)as->w * as->h * bpp_of(as->format));
        break;
    }
    case RCS_OP_UPLOAD: {
        rcs_upload a; memcpy(&a, body, sizeof a);
        rcs_surface *s = &c->surfaces[a.slot];
        assert(s->live && rect_inside(&a.r, s->w, s->h));
        const uint8_t *p = body + sizeof a;
        damage_rect(s, &a.r);
        uint32_t bpp = bpp_of(s->format);
        for (int32_t j = 0; j < a.r.h; ++j)
            for (int32_t i = 0; i < a.r.w; ++i) {
                uint32_t v = 0;
                memcpy(&v, p, bpp); p += bpp;
                s->px[(size_t)(a.r.y + j) * s->w + (size_t)(a.r.x + i)] = v;
            }
        c->frame_upload += (uint32_t)((uint64_t)a.r.w * a.r.h * bpp);
        break;
    }
    case RCS_OP_SET_CLIP: {
        rcs_set_clip a; memcpy(&a, body, sizeof a);
        assert(c->surfaces[a.target].live && rect_inside(&a.r, c->surfaces[a.target].w, c->surfaces[a.target].h));
        c->clip_set = true; c->clip_target = a.target; c->clip = a.r;
        break;
    }
    case RCS_OP_CLEAR: {
        rcs_clear a; memcpy(&a, body, sizeof a);
        rcs_surface *t = &c->surfaces[a.target];
        assert(t->live);
        rcs_rect reg = {0, 0, (int32_t)t->w, (int32_t)t->h};
        if (c->clip_set && c->clip_target == a.target) reg = c->clip;
        fill(t, &reg, a.color);
        c->frame_pixel_ops += (uint64_t)reg.w * reg.h;
        break;
    }
    case RCS_OP_FILL_RECT: {
        rcs_fill_rect a; memcpy(&a, body, sizeof a);
        rcs_surface *t = &c->surfaces[a.target];
        assert(t->live);
        rcs_rect reg = {0, 0, (int32_t)t->w, (int32_t)t->h}, d;
        if (c->clip_set && c->clip_target == a.target) reg = c->clip;
        if (!intersect(&a.r, &reg, &d)) break;
        fill(t, &d, a.color);
        c->frame_pixel_ops += (uint64_t)d.w * d.h;
        break;
    }
    case RCS_OP_BLIT: {
        rcs_blit a; memcpy(&a, body, sizeof a);
        rcs_surface *t = &c->surfaces[a.target], *s = &c->surfaces[a.src];
        assert(t->live && s->live && t != s && rect_inside(&a.sr, s->w, s->h));
        rcs_rect reg = {0, 0, (int32_t)t->w, (int32_t)t->h}, dst = {a.dx, a.dy, a.sr.w, a.sr.h}, d;
        if (c->clip_set && c->clip_target == a.target) reg = c->clip;
        if (!intersect(&dst, &reg, &d)) break;
        assert(rect_inside(&d, t->w, t->h));
        damage_rect(t, &d);
        int32_t ox = d.x - a.dx, oy = d.y - a.dy;  /* offset into the source rect after clipping */
#ifdef RCS_USE_SUPEROPS
        {
            mxs_surface dv = mxs_view(t);
            mxs_csurface sv = { mxs_cspan_of(s->px, (uint64_t)s->w * s->h), s->w, s->h, s->w };
            mxs_rect sr = { a.sr.x + ox, a.sr.y + oy, d.w, d.h };
            mxs_must(mxs_blit(&dv, d.x, d.y, &sv, &sr, (mxs_blend)a.blend));
            c->frame_pixel_ops += (uint64_t)d.w * d.h * (a.blend != RCS_BLEND_COPY ? 2u : 1u);
            break;
        }
#endif
        for (int32_t j = 0; j < d.h; ++j) {
            const uint32_t *srow = s->px + (size_t)(a.sr.y + oy + j) * s->w + (size_t)(a.sr.x + ox);
            uint32_t *drow = t->px + (size_t)(d.y + j) * t->w + (size_t)d.x;
            for (int32_t i = 0; i < d.w; ++i) drow[i] = blend_px(drow[i], srow[i], a.blend);
        }
        c->frame_pixel_ops += (uint64_t)d.w * d.h * (a.blend != RCS_BLEND_COPY ? 2u : 1u);
        break;
    }
    case RCS_OP_LINE: {
        rcs_line a; memcpy(&a, body, sizeof a);
        rcs_surface *t = &c->surfaces[a.target];
        assert(t->live);
        rcs_rect reg = {0, 0, (int32_t)t->w, (int32_t)t->h};
        if (c->clip_set && c->clip_target == a.target) reg = c->clip;
        int64_t x0 = a.x0, y0 = a.y0, x1 = a.x1, y1 = a.y1;
        int64_t dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
        int64_t dy = y1 > y0 ? -(y1 - y0) : -(y0 - y1), sy = y0 < y1 ? 1 : -1;
        int64_t err = dx + dy;
        uint64_t steps = 0;
        for (;;) {
            if (x0 >= reg.x && y0 >= reg.y && x0 < (int64_t)reg.x + reg.w && y0 < (int64_t)reg.y + reg.h) {
                t->px[(size_t)y0 * t->w + (size_t)x0] = a.color;
                damage_px(t, x0, y0);
            }
            steps++;
            if (x0 == x1 && y0 == y1) break;
            int64_t e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
        c->frame_pixel_ops += steps;
        break;
    }
    case RCS_OP_DRAW_TRIANGLES: {
        rcs_draw_triangles a; memcpy(&a, body, sizeof a);
        rcs_surface *t = &c->surfaces[a.target];
        const rcs_asset *m = &c->assets[a.mesh_slot];
        assert(t->live && m->kind == RCS_ASSET_MESH && (uint64_t)a.first_tri + a.tri_count <= m->tri_count);
        rcs_rect reg = {0, 0, (int32_t)t->w, (int32_t)t->h};
        if (c->clip_set && c->clip_target == a.target) reg = c->clip;
        if (!m->mesh) { c->frame_triangles += a.tri_count; break; }  /* not streamed in yet: draw nothing */
        for (uint32_t k = 0; k < a.tri_count; ++k) {
            const int32_t *v = m->mesh + (size_t)(a.first_tri + k) * 7;
            /* 16.16 fixed to integer pixels; flat colour (the reference ignores the texture). */
            int64_t x[3] = {v[0] >> 16, v[2] >> 16, v[4] >> 16}, y[3] = {v[1] >> 16, v[3] >> 16, v[5] >> 16};
            uint32_t color = (uint32_t)v[6];
            int64_t minx = x[0], maxx = x[0], miny = y[0], maxy = y[0];
            for (int i = 1; i < 3; ++i) { if (x[i] < minx) minx = x[i]; if (x[i] > maxx) maxx = x[i]; if (y[i] < miny) miny = y[i]; if (y[i] > maxy) maxy = y[i]; }
            if (minx < reg.x) minx = reg.x;
            if (miny < reg.y) miny = reg.y;
            if (maxx > (int64_t)reg.x + reg.w - 1) maxx = (int64_t)reg.x + reg.w - 1;
            if (maxy > (int64_t)reg.y + reg.h - 1) maxy = (int64_t)reg.y + reg.h - 1;
            int64_t area = (x[1] - x[0]) * (y[2] - y[0]) - (x[2] - x[0]) * (y[1] - y[0]);
            if (area == 0 || maxx < minx || maxy < miny) continue;
            { rcs_rect bb = {(int32_t)minx, (int32_t)miny, (int32_t)(maxx - minx + 1), (int32_t)(maxy - miny + 1)}; damage_rect(t, &bb); }
#ifdef RCS_USE_SUPEROPS
            { mxs_surface tv = mxs_view(t); mxs_rect cr = {reg.x, reg.y, reg.w, reg.h}; mxs_must(mxs_triangle(&tv, &cr, x, y, color)); continue; }
#endif
            for (int64_t py = miny; py <= maxy; ++py)
                for (int64_t px = minx; px <= maxx; ++px) {
                    int64_t w0 = (x[1] - px) * (y[2] - py) - (x[2] - px) * (y[1] - py);
                    int64_t w1 = (x[2] - px) * (y[0] - py) - (x[0] - px) * (y[2] - py);
                    int64_t w2 = area - w0 - w1;
                    bool inside = area > 0 ? (w0 >= 0 && w1 >= 0 && w2 >= 0) : (w0 <= 0 && w1 <= 0 && w2 <= 0);
                    if (inside) t->px[(size_t)py * t->w + (size_t)px] = color;
                }
        }
        c->frame_triangles += a.tri_count;
        break;
    }
    case RCS_OP_BEGIN_FRAME: {
        rcs_begin_frame a; memcpy(&a, body, sizeof a);
        assert(!c->in_frame);
        if (c->have_presented && c->surfaces[c->last_present.slot].live && c->surfaces[c->last_present.slot].tiles == c->last_present.tiles)
            damage_clear(&c->surfaces[c->last_present.slot]);   /* the compositor consumed it between PRESENT and this BEGIN_FRAME */
        c->in_frame = true; c->frame_no = a.frame_no;
        c->frame_pixel_ops = 0; c->frame_triangles = 0; c->frame_upload = 0; c->frame_bind = 0; c->clip_set = false;
        break;
    }
    case RCS_OP_PRESENT: {
        rcs_present a; memcpy(&a, body, sizeof a);
        assert(c->in_frame && a.frame_no == c->frame_no);
        /* Hand-off to the compositing container happens here (dirty-bit canvas thread). */
        { rcs_surface *t = &c->surfaces[a.target];
          c->last_present = (rcs_present_info){ a.target, a.frame_no, t->tw, t->th, t->dirty, t->tiles }; }
        c->in_frame = false; c->have_presented = true; c->last_presented = a.frame_no; c->clip_set = false; c->presents++;
        break;
    }
    case RCS_OP_FENCE: {
        rcs_fence a; memcpy(&a, body, sizeof a);
        /* The reference completes fences synchronously; the service signals the client's notification. */
        (void)a;
        break;
    }
    case RCS_OP_PROMISE_DROP: {
        rcs_promise_drop a; memcpy(&a, body, sizeof a);
        c->rights &= ~a.rights_mask;
        break;
    }
    default:
        abort();  /* unreachable: the validator rejected it */
    }
}

/* ---- submit --------------------------------------------------------------- */

static rcs_result reject(uint32_t index, uint32_t err) {
    rcs_result r; memset(&r, 0, sizeof r);
    r.magic = RCS_RESULT_MAGIC; r.status = RCS_REJECTED; r.failing_index = index; r.error = err;
    return r;
}

rcs_result rcs_submit(rcs_client *c, const uint8_t *bytes, size_t n) {
    const uint32_t NOIDX = 0xFFFFFFFFu;
    if (n > RCS_MAX_BATCH_BYTES) return reject(NOIDX, RCS_E_TOO_LARGE);
    if (n < sizeof(rcs_batch_hdr)) return reject(NOIDX, RCS_E_LENGTH_MISMATCH);
    rcs_batch_hdr bh; memcpy(&bh, bytes, sizeof bh);
    if (bh.magic != RCS_BATCH_MAGIC) return reject(NOIDX, RCS_E_BAD_MAGIC);
    if (bh.version != RCS_VERSION) return reject(NOIDX, RCS_E_BAD_VERSION);
    if (bh.kind != RCS_BATCH_RESOURCE && bh.kind != RCS_BATCH_FRAME) return reject(NOIDX, RCS_E_BAD_KIND);
    if (bh.reserved != 0) return reject(NOIDX, RCS_E_RESERVED_NONZERO);
    if (bh.total_bytes != n) return reject(NOIDX, RCS_E_LENGTH_MISMATCH);
    if (bh.cmd_count > RCS_MAX_BATCH_CMDS) return reject(NOIDX, RCS_E_TOO_MANY_CMDS);

    /* Pass 1: walk and validate against a shadow of the client's state. */
    shadow sh; shadow_from(&sh, c);
    size_t off = sizeof bh;
    for (uint32_t i = 0; i < bh.cmd_count; ++i) {
        if (n - off < sizeof(rcs_cmd_hdr)) return reject(i, RCS_E_LENGTH_MISMATCH);
        rcs_cmd_hdr ch; memcpy(&ch, bytes + off, sizeof ch);
        off += sizeof ch;
        if (ch.version != RCS_VERSION) return reject(i, RCS_E_BAD_VERSION);
        if (ch.flags != 0 || ch.reserved != 0) return reject(i, RCS_E_RESERVED_NONZERO);
        if (ch.seq != i) return reject(i, RCS_E_BAD_SEQ);
        uint32_t fixed = rcs_body_size(ch.opcode);
        if (fixed == 0) return reject(i, RCS_E_BAD_OPCODE);
        if (!class_ok(ch.opcode, bh.kind)) return reject(i, RCS_E_WRONG_CLASS);
        if (ch.opcode == RCS_OP_UPLOAD) {
            if (ch.body_len < fixed || ch.body_len > fixed + RCS_MAX_UPLOAD_BYTES) return reject(i, RCS_E_BAD_BODY_LEN);
        } else if (ch.body_len != fixed) {
            return reject(i, RCS_E_BAD_BODY_LEN);
        }
        if (n - off < ch.body_len) return reject(i, RCS_E_LENGTH_MISMATCH);
        uint32_t e = validate_cmd(&sh, c, ch.opcode, bytes + off, ch.body_len);
        if (e != RCS_E_NONE) return reject(i, e);
        off += ch.body_len;
    }
    if (off != n) return reject(NOIDX, RCS_E_LENGTH_MISMATCH);   /* trailing bytes */

    /* Pass 2: execute. Every command has been validated; the executor asserts that. */
    off = sizeof bh;
    for (uint32_t i = 0; i < bh.cmd_count; ++i) {
        rcs_cmd_hdr ch; memcpy(&ch, bytes + off, sizeof ch);
        off += sizeof ch;
        exec_cmd(c, ch.opcode, bytes + off);
        off += ch.body_len;
    }
    /* The executor's own accounting must agree with the validator's. */
    assert(c->in_frame == sh.in_frame);
    assert(c->rights == sh.rights);
    assert(c->surface_pixels == sh.surface_pixels);
    if (c->in_frame) { assert(c->frame_pixel_ops == sh.pixel_ops); assert(c->frame_triangles == sh.triangles); }

    rcs_result r; memset(&r, 0, sizeof r);
    r.magic = RCS_RESULT_MAGIC; r.status = RCS_OK; r.failing_index = NOIDX; r.error = RCS_E_NONE;
    r.pixel_ops = sh.batch_pixel_ops; r.triangles = sh.batch_triangles; r.upload_bytes = sh.batch_upload; r.fences_done = sh.fences; r.bind_bytes = sh.batch_bind;
    return r;
}
