#define _GNU_SOURCE
#include "mx_render.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

uint64_t mx_now_ns(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec; }

int mx_render_init(mx_render_service *s, asset_store *store, mx_render_sink *sink) {
    memset(s, 0, sizeof *s);
    s->store = store; s->sink = sink;
    s->scratch = (uint8_t *)malloc(RCS_MAX_BATCH_BYTES);
    return s->scratch ? 0 : -1;
}

void mx_render_detach(mx_render_service *s, int ci) {
    if (ci < 0 || (unsigned)ci >= MX_RENDER_MAX_CLIENTS || !s->clients[ci].live) return;
    mx_render_client *c = &s->clients[ci];
    if (c->ring) atomic_store_explicit(&c->ring->revoked, 1u, memory_order_release);
    for (uint32_t i = 0; i < RCS_MAX_ASSETS; ++i)
        if (c->grants[i].granted && s->store) asset_close(s->store, c->grants[i].h);
    rcs_client_free(&c->rcs);
    if (c->canvas_live) gc_free(&c->canvas);
    memset(c, 0, sizeof *c);
}

void mx_render_free(mx_render_service *s) {
    for (unsigned i = 0; i < MX_RENDER_MAX_CLIENTS; ++i) mx_render_detach(s, (int)i);
    free(s->scratch); s->scratch = NULL;
}

int mx_render_attach(mx_render_service *s, mx_ring *ring, uint64_t badge, uint32_t rights) {
    if (!ring || ring->magic != MX_RING_MAGIC || ring->version != MX_RING_VERSION) return -1;
    for (unsigned i = 0; i < MX_RENDER_MAX_CLIENTS; ++i) {
        mx_render_client *c = &s->clients[i];
        if (c->live) continue;
        memset(c, 0, sizeof *c);
        c->live = true; c->badge = badge; c->ring = ring; c->display_slot = RCS_SLOT_NONE;
        rcs_client_init(&c->rcs, rights & RCS_R_ALL);
        return (int)i;
    }
    return -1;
}

static int grant(mx_render_service *s, int ci, uint32_t slot, const uint8_t hash[ASSET_HASH_BYTES], uint32_t mip, uint8_t prio, uint32_t kind) {
    if (!s->store || ci < 0 || (unsigned)ci >= MX_RENDER_MAX_CLIENTS || !s->clients[ci].live || slot >= RCS_MAX_ASSETS || mip >= ASSET_MAX_MIPS) return ASSET_ERR_ARG;
    mx_render_client *c = &s->clients[ci];
    if (c->grants[slot].granted) return ASSET_ERR_ARG;
    asset_handle h; int rc = asset_open(s->store, hash, &h);
    if (rc != ASSET_OK) return rc;
    asset_status st; rc = asset_query(s->store, h, &st);
    if (rc != ASSET_OK || mip >= st.mips) { asset_close(s->store, h); return rc != ASSET_OK ? rc : ASSET_ERR_ARG; }
    rcs_asset *a = &c->rcs.assets[slot];
    if (kind == RCS_ASSET_IMAGE) {
        if (st.kind != ASSET_KIND_IMAGE || st.stride_px != (st.width >> mip ? st.width >> mip : 1u)) { asset_close(s->store, h); return ASSET_ERR_FORMAT; }
        /* rcs wants w*h row-major: the packer emits stride == width for untiled images. */
        a->kind = RCS_ASSET_IMAGE; a->w = st.width >> mip; a->h = st.height >> mip; if (!a->w) a->w = 1; if (!a->h) a->h = 1;
        a->format = RCS_FMT_ARGB8888; a->image = NULL; a->mesh = NULL; a->tri_count = 0;
    } else {
        if (st.kind != ASSET_KIND_BLOB || st.bytes[0] % 28u != 0 || st.bytes[0] == 0) { asset_close(s->store, h); return ASSET_ERR_FORMAT; }
        a->kind = RCS_ASSET_MESH; a->tri_count = (uint32_t)(st.bytes[0] / 28u); a->w = a->h = 0; a->format = 0; a->image = NULL; a->mesh = NULL;
    }
    rc = asset_want(s->store, h, mip, prio);
    if (rc != ASSET_OK) { memset(a, 0, sizeof *a); asset_close(s->store, h); return rc; }
    c->grants[slot] = (mx_render_grant){ true, h, mip, kind, false };
    return ASSET_OK;
}
int mx_render_grant_image(mx_render_service *s, int ci, uint32_t slot, const uint8_t hash[ASSET_HASH_BYTES], uint32_t mip, uint8_t prio) { return grant(s, ci, slot, hash, mip, prio, RCS_ASSET_IMAGE); }
int mx_render_grant_mesh (mx_render_service *s, int ci, uint32_t slot, const uint8_t hash[ASSET_HASH_BYTES], uint8_t prio)               { return grant(s, ci, slot, hash, 0, prio, RCS_ASSET_MESH); }

/* After asset_poll: re-map every grant. Pointers from asset_map are valid until the next poll,
 * and the next poll happens only at the top of the next pump, after every batch of this pump. */
static void refresh_grants(mx_render_service *s) {
    for (unsigned i = 0; i < MX_RENDER_MAX_CLIENTS; ++i) {
        mx_render_client *c = &s->clients[i];
        if (!c->live) continue;
        for (uint32_t slot = 0; slot < RCS_MAX_ASSETS; ++slot) {
            mx_render_grant *g = &c->grants[slot];
            if (!g->granted) continue;
            const void *p = NULL; size_t len = 0;
            bool res = asset_map(s->store, g->h, g->mip, &p, &len) == ASSET_OK;
            rcs_asset *a = &c->rcs.assets[slot];
            if (res && g->kind == RCS_ASSET_IMAGE && len < (size_t)a->w * a->h * 4u) res = false;      /* store says resident but shorter than the geometry: never trust it */
            if (res && g->kind == RCS_ASSET_MESH && len < (size_t)a->tri_count * 28u) res = false;
            if (g->kind == RCS_ASSET_IMAGE) a->image = res ? (const uint32_t *)p : NULL; else a->mesh = res ? (const int32_t *)p : NULL;
            if (res != g->resident) {
                g->resident = res;
                uint64_t bit = (uint64_t)1 << (slot % 64);
                if (res) atomic_fetch_or_explicit(&c->ring->ready_mask[slot / 64], bit, memory_order_release);
                else     atomic_fetch_and_explicit(&c->ring->ready_mask[slot / 64], ~bit, memory_order_release);
            }
        }
    }
}

static int sink_tile(void *user, uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint32_t *px, uint32_t stride) {
    struct { mx_render_service *s; uint32_t ci, frame; } *ctx = user;
    mx_render_sink *k = ctx->s->sink;
    return k && k->tile ? k->tile(k, ctx->ci, ctx->frame, x, y, w, h, px, stride) : 0;
}

/* PRESENT happened inside rcs_submit: move the damaged tiles into the display canvas and present. */
static void composite(mx_render_service *s, uint32_t ci) {
    mx_render_client *c = &s->clients[ci];
    const rcs_present_info *pi = &c->rcs.last_present;
    const rcs_surface *sf = &c->rcs.surfaces[pi->slot];
    if (!sf->live || sf->format != RCS_FMT_ARGB8888) return;
    bool fresh = false;
    if (!c->canvas_live || c->canvas.w != sf->w || c->canvas.h != sf->h || c->display_slot != pi->slot) {
        if (c->canvas_live) gc_free(&c->canvas);
        if (gc_init(&c->canvas, sf->w, sf->h) != 0) { c->canvas_live = false; return; }
        c->canvas_live = true; c->display_slot = pi->slot; fresh = true;
    }
    gc_canvas *g = &c->canvas;
    if (fresh) {
        memcpy(g->work, sf->px, (size_t)sf->w * sf->h * 4u);
        gc_invalidate_all(g);                    /* new display: key frame, every tile goes out */
    } else {
        /* Only the tiles the validator saw a draw touch are copied; the canvas then XOR-filters
         * the ones whose pixels did not actually change (A->B->A, redraw of the same sprite). */
        for (uint32_t ty = 0; ty < pi->th; ++ty) {
            uint32_t y0 = ty * RCS_TILE, hh = sf->h - y0 < RCS_TILE ? sf->h - y0 : RCS_TILE;
            for (uint32_t tx = 0; tx < pi->tw; ++tx) {
                uint32_t bit = ty * pi->tw + tx;
                if (!(pi->tiles[bit >> 3] & (1u << (bit & 7)))) continue;
                uint32_t x0 = tx * RCS_TILE, ww = sf->w - x0 < RCS_TILE ? sf->w - x0 : RCS_TILE;
                for (uint32_t r = 0; r < hh; ++r)
                    memcpy(g->work + (size_t)(y0 + r) * sf->w + x0, sf->px + (size_t)(y0 + r) * sf->w + x0, (size_t)ww * 4u);
            }
        }
        gc_damage_bits(g, pi->tiles, pi->tw, pi->th);
    }
    struct { mx_render_service *s; uint32_t ci, frame; } ctx = { s, ci, pi->frame_no };
    gc_present(g, sink_tile, &ctx);
    c->stats.tiles_emitted += g->last.tiles_emitted; c->stats.bytes_emitted += g->last.bytes_emitted; c->stats.presents++;
    if (s->sink && s->sink->frame_done) s->sink->frame_done(s->sink, ci, pi->frame_no, &g->last, g->w, g->h);
}

uint32_t mx_render_pump(mx_render_service *s) {
    if (s->store) { asset_poll(s->store, s->events, sizeof s->events / sizeof s->events[0]); s->polls++; refresh_grants(s); }
    uint32_t n = 0;
    for (unsigned ci = 0; ci < MX_RENDER_MAX_CLIENTS; ++ci) {
        mx_render_client *c = &s->clients[ci];
        if (!c->live) continue;
        for (uint32_t k = 0; k < MX_RING_DESCS; ++k) {       /* bounded per pump: a flooding client cannot starve the others */
            size_t len = 0;
            uint64_t t0 = mx_now_ns();
            mx_ring_take tk = mx_ring_take_batch(c->ring, s->scratch, &len);
            if (tk == MX_RING_EMPTY) break;
            uint64_t t1 = mx_now_ns();
            rcs_result r;
            if (tk == MX_RING_BAD_DESC) {
                memset(&r, 0, sizeof r); r.magic = RCS_RESULT_MAGIC; r.status = RCS_REJECTED; r.failing_index = 0xFFFFFFFFu; r.error = RCS_E_TOO_LARGE;
                c->stats.bad_descs++;
            } else {
                uint64_t presents_before = c->rcs.presents;
                r = rcs_submit(&c->rcs, s->scratch, len);
                uint64_t t2 = mx_now_ns();
                c->stats.ns_submit += t2 - t1;
                if (r.status == RCS_OK) {
                    c->stats.pixel_ops += r.pixel_ops;
                    if (c->rcs.presents != presents_before) { composite(s, ci); c->stats.ns_composite += mx_now_ns() - t2; c->stats.last_present_ns = mx_now_ns() - t1; }
                } else c->stats.rejected++;
            }
            c->stats.ns_copy += t1 - t0;
            c->stats.batches++;
            mx_ring_put_result(c->ring, &r);
            ++n;
        }
    }
    return n;
}

uint64_t mx_render_display_hash(const mx_render_service *s, int ci) {
    if (ci < 0 || (unsigned)ci >= MX_RENDER_MAX_CLIENTS || !s->clients[ci].canvas_live) return 0;
    return gc_ghost_hash(&s->clients[ci].canvas);
}
