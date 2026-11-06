#include "probe.h"
void gp_init(gp_state *s) { s->x = GP_W / 2u; s->y = GP_H / 2u; s->frame = 0u; s->keys = 0; }
void gp_tick(gp_state *s, uint8_t keys) {
    keys &= (GP_LEFT | GP_RIGHT | GP_UP | GP_DOWN);
    s->keys = keys;
    if ((keys & GP_LEFT) && !(keys & GP_RIGHT) && s->x > 3u) --s->x;
    if ((keys & GP_RIGHT) && !(keys & GP_LEFT) && s->x < GP_W - 4u) ++s->x;
    if ((keys & GP_UP) && !(keys & GP_DOWN) && s->y > 3u) --s->y;
    if ((keys & GP_DOWN) && !(keys & GP_UP) && s->y < GP_H - 4u) ++s->y;
    ++s->frame;
}
void gp_render(const gp_state *s, uint32_t *pixels) {
    /* Deliberately simple software renderer: horizon, patterned walls, and player. */
    for (uint32_t y = 0; y < GP_H; ++y) {
        for (uint32_t x = 0; x < GP_W; ++x) {
            uint32_t c = y < 45u ? 0xff18283au : 0xff28221cu;
            if (y >= 18u && y < 82u && ((x / 20u) % 3u) == 0u)
                c = ((x / 5u + y / 7u) & 1u) ? 0xff65513bu : 0xff4b3b2bu;
            if (x >= s->x - 3u && x <= s->x + 3u && y >= s->y - 3u && y <= s->y + 3u)
                c = 0xffffbb33u;
            pixels[y * GP_W + x] = c;
        }
    }
}
uint64_t gp_hash(const uint32_t *pixels) {
    uint64_t h = UINT64_C(14695981039346656037);
    for (uint32_t i = 0; i < GP_PIXELS; ++i) {
        uint32_t v = pixels[i];
        for (unsigned b = 0; b < 4; ++b) { h ^= (uint8_t)(v >> (8u * b)); h *= UINT64_C(1099511628211); }
    }
    return h;
}
