/* mx_rcs_build.h - client-side batch builder for the RCS wire format (rcs.h).
 * Header only. Writes directly into a ring reservation or any buffer. The
 * builder is a convenience for the game; the service never trusts what it
 * produced (every field is re-validated by rcs_submit). */
#ifndef MX_RCS_BUILD_H
#define MX_RCS_BUILD_H
#include "../contracts/gpu-commands-ref/rcs.h"
#include <string.h>

typedef struct { uint8_t *buf; uint32_t cap, len, count; bool overflow; } mxb;

static inline void mxb_begin(mxb *b, uint8_t *buf, uint32_t cap, uint8_t kind) {
    b->buf = buf; b->cap = cap; b->len = 0; b->count = 0; b->overflow = false;
    rcs_batch_hdr h = { RCS_BATCH_MAGIC, RCS_VERSION, kind, 0, 0, 0 };
    if (cap < sizeof h) { b->overflow = true; return; }
    memcpy(buf, &h, sizeof h); b->len = sizeof h;
}
static inline void mxb_cmd(mxb *b, uint16_t op, const void *body, uint32_t body_len, const void *payload, uint32_t payload_len) {
    uint32_t need = (uint32_t)sizeof(rcs_cmd_hdr) + body_len + payload_len;
    if (b->overflow || b->cap - b->len < need) { b->overflow = true; return; }
    rcs_cmd_hdr h = { op, RCS_VERSION, 0, body_len + payload_len, b->count, 0 };
    memcpy(b->buf + b->len, &h, sizeof h); b->len += sizeof h;
    memcpy(b->buf + b->len, body, body_len); b->len += body_len;
    if (payload_len) { memcpy(b->buf + b->len, payload, payload_len); b->len += payload_len; }
    b->count++;
}
/* Finish: patch counts; returns total bytes or 0 on overflow. */
static inline uint32_t mxb_end(mxb *b) {
    if (b->overflow) return 0;
    rcs_batch_hdr h; memcpy(&h, b->buf, sizeof h); h.cmd_count = b->count; h.total_bytes = b->len; memcpy(b->buf, &h, sizeof h);
    return b->len;
}
#define MXB_FIXED(b, op, T, ...) do { T _a = __VA_ARGS__; mxb_cmd((b), (op), &_a, sizeof _a, NULL, 0); } while (0)
static inline void mxb_create_surface(mxb *b, uint32_t slot, uint32_t w, uint32_t h, uint32_t fmt) { MXB_FIXED(b, RCS_OP_CREATE_SURFACE, rcs_create_surface, { slot, w, h, fmt }); }
static inline void mxb_destroy_surface(mxb *b, uint32_t slot) { MXB_FIXED(b, RCS_OP_DESTROY_SURFACE, rcs_destroy_surface, { slot }); }
static inline void mxb_bind_asset(mxb *b, uint32_t asset, uint32_t surface) { MXB_FIXED(b, RCS_OP_BIND_ASSET, rcs_bind_asset, { asset, surface }); }
static inline void mxb_upload(mxb *b, uint32_t slot, rcs_rect r, const void *px, uint32_t bytes) { rcs_upload a = { slot, r }; mxb_cmd(b, RCS_OP_UPLOAD, &a, sizeof a, px, bytes); }
static inline void mxb_set_clip(mxb *b, uint32_t t, rcs_rect r) { MXB_FIXED(b, RCS_OP_SET_CLIP, rcs_set_clip, { t, r }); }
static inline void mxb_clear(mxb *b, uint32_t t, uint32_t color) { MXB_FIXED(b, RCS_OP_CLEAR, rcs_clear, { t, color }); }
static inline void mxb_fill_rect(mxb *b, uint32_t t, rcs_rect r, uint32_t color) { MXB_FIXED(b, RCS_OP_FILL_RECT, rcs_fill_rect, { t, r, color }); }
static inline void mxb_blit(mxb *b, uint32_t t, int32_t dx, int32_t dy, uint32_t src, rcs_rect sr, uint32_t blend) { MXB_FIXED(b, RCS_OP_BLIT, rcs_blit, { t, dx, dy, src, sr, blend, 0 }); }
static inline void mxb_line(mxb *b, uint32_t t, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color) { MXB_FIXED(b, RCS_OP_LINE, rcs_line, { t, x0, y0, x1, y1, color }); }
static inline void mxb_draw_triangles(mxb *b, uint32_t t, uint32_t mesh, uint32_t first, uint32_t count, uint32_t tex, uint32_t shading) { MXB_FIXED(b, RCS_OP_DRAW_TRIANGLES, rcs_draw_triangles, { t, mesh, first, count, tex, shading }); }
static inline void mxb_begin_frame(mxb *b, uint32_t frame_no) { MXB_FIXED(b, RCS_OP_BEGIN_FRAME, rcs_begin_frame, { frame_no }); }
static inline void mxb_present(mxb *b, uint32_t t, uint32_t frame_no) { MXB_FIXED(b, RCS_OP_PRESENT, rcs_present, { t, frame_no }); }
static inline void mxb_fence(mxb *b, uint32_t token) { MXB_FIXED(b, RCS_OP_FENCE, rcs_fence, { token }); }
static inline void mxb_promise_drop(mxb *b, uint32_t mask) { MXB_FIXED(b, RCS_OP_PROMISE_DROP, rcs_promise_drop, { mask }); }
#endif
