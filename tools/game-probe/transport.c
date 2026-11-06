#include "transport.h"
int gt_input_push(gt_input *q, gt_event event) {
    if (!q || (event.down != 1u && event.down != 2u) ||
        (event.key != GT_KEY_LEFT && event.key != GT_KEY_RIGHT &&
         event.key != GT_KEY_UP && event.key != GT_KEY_DOWN)) return GT_ERR;
    if (q->count >= GT_QUEUE) return GT_FULL;
    q->events[(q->read + q->count) % GT_QUEUE] = event;
    ++q->count;
    return GT_OK;
}
int gt_input_pop(gt_input *q, gt_event *out) {
    if (!q || !out) return GT_ERR;
    if (!q->count) return GT_EMPTY;
    *out = q->events[q->read];
    q->read = (q->read + 1u) % GT_QUEUE;
    --q->count;
    return GT_OK;
}
static uint64_t tilehash(const uint32_t *p, uint32_t tx, uint32_t ty) {
    uint64_t h = UINT64_C(14695981039346656037);
    uint32_t ex = tx + GT_TILE < GT_WIDTH ? tx + GT_TILE : GT_WIDTH;
    uint32_t ey = ty + GT_TILE < GT_HEIGHT ? ty + GT_TILE : GT_HEIGHT;
    for (uint32_t y = ty; y < ey; ++y)
        for (uint32_t x = tx; x < ex; ++x) {
            uint32_t v = p[y * GT_WIDTH + x];
            for (unsigned b = 0; b < 4u; ++b) {
                h ^= (uint8_t)(v >> (b * 8u));
                h *= UINT64_C(1099511628211);
            }
        }
    return h;
}
int gt_present(gt_surface *s, const uint32_t *src, size_t bytes, uint32_t width,
               uint32_t height, uint32_t stride_pixels, uint32_t *changed) {
    if (!s || !src || !changed || width != GT_WIDTH || height != GT_HEIGHT ||
        stride_pixels != GT_WIDTH || bytes != (size_t)GT_PIXELS * sizeof(uint32_t) ||
        s->frame == UINT64_MAX) return GT_ERR;
    /* Copy before hashing so service results cannot alias the caller's buffer. */
    for (size_t i = 0; i < GT_PIXELS; ++i) s->pixels[i] = src[i];
    uint32_t count = 0;
    for (uint32_t ty = 0; ty < GT_TILE_ROWS; ++ty)
        for (uint32_t tx = 0; tx < GT_TILE_COLS; ++tx) {
            uint32_t i = ty * GT_TILE_COLS + tx;
            uint64_t h = tilehash(s->pixels, tx * GT_TILE, ty * GT_TILE);
            if (!s->valid || s->hashes[i] != h) ++count;
            s->hashes[i] = h;
        }
    s->valid = 1u;
    ++s->frame;
    *changed = count;
    return GT_OK;
}
