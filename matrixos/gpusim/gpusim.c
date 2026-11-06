/* gpusim.c - simulated memory-based GPU. See gpusim.h. */
#define _GNU_SOURCE
#include "gpusim.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    bool         granted;
    asset_handle h;
    uint32_t     kind;        /* RCS_ASSET_IMAGE or RCS_ASSET_MESH */
} grant;

struct gpusim {
    rcs_client    client;          /* ~ surfaces + asset table + frame state */
    asset_store  *store;
    grant         grants[RCS_MAX_ASSETS];
    uint8_t      *ring_copy;       /* service-private copy of the delivered batch */
    uint32_t     *front;           /* scanout ("display memory") */
    uint32_t      fw, fh;
    uint64_t      presents_seen;
    gpusim_stats  st;
};

static uint64_t now_ns(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

int gpusim_open(const gpusim_config *cfg, gpusim **out) {
    if (!cfg || !out) return ASSET_ERR_ARG;
    gpusim *g = (gpusim *)calloc(1, sizeof *g);
    if (!g) return ASSET_ERR_NOMEM;
    g->ring_copy = (uint8_t *)malloc(RCS_MAX_BATCH_BYTES);
    if (!g->ring_copy) { free(g); return ASSET_ERR_NOMEM; }
    rcs_client_init(&g->client, cfg->rights);
    mxs_set_tier(cfg->tier);
    if (cfg->store_root) {
        asset_store_config sc; memset(&sc, 0, sizeof sc);
        sc.plan = cfg->plan;
        int rc = asset_store_open(cfg->store_root, &sc, &g->store);
        if (rc != ASSET_OK) { free(g->ring_copy); free(g); return rc; }
    }
    *out = g;
    return ASSET_OK;
}

void gpusim_close(gpusim *g) {
    if (!g) return;
    rcs_client_free(&g->client);
    if (g->store) asset_store_close(g->store);
    free(g->ring_copy); free(g->front); free(g);
}

int gpusim_grant(gpusim *g, uint32_t slot, const uint8_t hash[ASSET_HASH_BYTES], uint32_t kind) {
    if (!g || !g->store || slot >= RCS_MAX_ASSETS || g->grants[slot].granted) return ASSET_ERR_ARG;
    if (kind != RCS_ASSET_IMAGE && kind != RCS_ASSET_MESH) return ASSET_ERR_ARG;
    asset_handle h;
    int rc = asset_open(g->store, hash, &h);            /* manifest read + SHA-256 check */
    if (rc != ASSET_OK) return rc;
    asset_status st;
    if ((rc = asset_query(g->store, h, &st)) != ASSET_OK) { asset_close(g->store, h); return rc; }
    rcs_asset a; memset(&a, 0, sizeof a);
    a.kind = kind;
    if (kind == RCS_ASSET_IMAGE) {
        if (st.kind != ASSET_KIND_IMAGE || st.stride_px != st.width || st.width > RCS_MAX_DIM || st.height > RCS_MAX_DIM) {
            asset_close(g->store, h); return ASSET_ERR_FORMAT;
        }
        a.w = st.width; a.h = st.height; a.format = RCS_FMT_ARGB8888;
    } else {
        /* A mesh is a blob of whole 28-byte triangles; the table, not the client, owns tri_count. */
        if (st.kind != ASSET_KIND_BLOB || st.bytes[0] == 0 || st.bytes[0] % 28u != 0 || st.bytes[0] / 28u > UINT32_MAX) {
            asset_close(g->store, h); return ASSET_ERR_FORMAT;
        }
        a.tri_count = (uint32_t)(st.bytes[0] / 28u);
    }
    if ((rc = asset_want(g->store, h, 0, 0)) != ASSET_OK) { asset_close(g->store, h); return rc; }
    g->client.assets[slot] = a;     /* image/mesh stay NULL until resident */
    g->grants[slot] = (grant){ true, h, kind };
    return ASSET_OK;
}

uint32_t gpusim_poll(gpusim *g) {
    if (!g || !g->store) return 0;
    asset_event ev[64];
    while (asset_poll(g->store, ev, 64) == 64) { }
    /* Pointers from asset_map are valid until the next asset_poll: refresh them all now. */
    uint32_t resident = 0;
    for (uint32_t i = 0; i < RCS_MAX_ASSETS; ++i) {
        if (!g->grants[i].granted) continue;
        const void *p = NULL; size_t len = 0;
        rcs_asset *a = &g->client.assets[i];
        int rc = asset_map(g->store, g->grants[i].h, 0, &p, &len);
        bool ok = rc == ASSET_OK && p != NULL;
        if (ok && a->kind == RCS_ASSET_IMAGE) ok = len >= (size_t)a->w * a->h * 4u;
        if (ok && a->kind == RCS_ASSET_MESH)  ok = len >= (size_t)a->tri_count * 28u;
        a->image = ok && a->kind == RCS_ASSET_IMAGE ? (const uint32_t *)p : NULL;
        a->mesh  = ok && a->kind == RCS_ASSET_MESH  ? (const int32_t *)p  : NULL;
        resident += ok;
    }
    return resident;
}

/* Copy the dirty 16x16 tiles of the presented surface into the front buffer. */
static void scanout(gpusim *g) {
    const rcs_present_info *pi = &g->client.last_present;
    const rcs_surface *s = &g->client.surfaces[pi->slot];
    if (!s->live) return;
    bool full = false;
    if (!g->front || g->fw != s->w || g->fh != s->h) {
        free(g->front);
        g->front = (uint32_t *)malloc((size_t)s->w * s->h * 4u);
        if (!g->front) abort();
        g->fw = s->w; g->fh = s->h; full = true;
    }
    if (full) { memcpy(g->front, s->px, (size_t)s->w * s->h * 4u); g->st.scanout_tiles += (uint64_t)pi->tw * pi->th; return; }
    for (uint32_t ty = 0; ty < pi->th; ++ty) {
        for (uint32_t tx = 0; tx < pi->tw; ++tx) {
            size_t bit = (size_t)ty * pi->tw + tx;
            if (!(pi->tiles[bit >> 3] & (1u << (bit & 7)))) continue;
            /* Extend over the run of dirty tiles in this row: one memcpy per row segment. */
            uint32_t tx1 = tx;
            while (tx1 + 1 < pi->tw) { size_t b2 = bit + (tx1 + 1 - tx); if (!(pi->tiles[b2 >> 3] & (1u << (b2 & 7)))) break; tx1++; }
            uint32_t x0 = tx * RCS_TILE, x1 = (tx1 + 1) * RCS_TILE; if (x1 > s->w) x1 = s->w;
            uint32_t y0 = ty * RCS_TILE, y1 = y0 + RCS_TILE;     if (y1 > s->h) y1 = s->h;
            for (uint32_t y = y0; y < y1; ++y) memcpy(g->front + (size_t)y * s->w + x0, s->px + (size_t)y * s->w + x0, (size_t)(x1 - x0) * 4u);
            g->st.scanout_tiles += tx1 - tx + 1;
            tx = tx1;
        }
    }
}

rcs_result gpusim_submit(gpusim *g, const uint8_t *bytes, size_t n) {
    rcs_result r;
    uint64_t t0 = now_ns();
    if (n > RCS_MAX_BATCH_BYTES) {
        r = rcs_submit(&g->client, bytes, n);            /* rejects on length before reading anything */
    } else {
        memcpy(g->ring_copy, bytes, n);                  /* copy, then check */
        r = rcs_submit(&g->client, g->ring_copy, n);
    }
    uint64_t t1 = now_ns();
    g->st.submit_ns += t1 - t0; g->st.batches++;
    if (r.status != RCS_OK) g->st.rejected++;
    if (g->client.presents != g->presents_seen) {
        g->presents_seen = g->client.presents;
        scanout(g);
        g->st.frames++;
        g->st.scanout_ns += now_ns() - t1;
    }
    return r;
}

const uint32_t *gpusim_front(const gpusim *g, uint32_t *w, uint32_t *h) {
    if (w) *w = g->fw;
    if (h) *h = g->fh;
    return g->front;
}
void gpusim_stats_get(const gpusim *g, gpusim_stats *out) { *out = g->st; }
const rcs_client *gpusim_client(const gpusim *g) { return &g->client; }

/* ---- encoder ---- */
void gpusim_batch_begin(gpusim_batch *b, uint8_t kind) {
    rcs_batch_hdr h = { RCS_BATCH_MAGIC, RCS_VERSION, kind, 0, 0, 0 };
    memcpy(b->buf, &h, sizeof h); b->len = sizeof h; b->count = 0; b->overflow = 0;
}
void gpusim_batch_add(gpusim_batch *b, uint16_t op, const void *body, uint32_t body_len, const void *payload, uint32_t payload_len) {
    size_t need = sizeof(rcs_cmd_hdr) + (size_t)body_len + payload_len;
    if (b->overflow || need > sizeof b->buf - b->len) { b->overflow = 1; return; }
    rcs_cmd_hdr h = { op, RCS_VERSION, 0, body_len + payload_len, b->count, 0 };
    memcpy(b->buf + b->len, &h, sizeof h); b->len += sizeof h;
    memcpy(b->buf + b->len, body, body_len); b->len += body_len;
    if (payload_len) { memcpy(b->buf + b->len, payload, payload_len); b->len += payload_len; }
    b->count++;
}
size_t gpusim_batch_end(gpusim_batch *b) {
    if (b->overflow) return 0;
    rcs_batch_hdr h; memcpy(&h, b->buf, sizeof h);
    h.cmd_count = b->count; h.total_bytes = (uint32_t)b->len;
    memcpy(b->buf, &h, sizeof h);
    return b->len;
}
