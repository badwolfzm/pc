/* Deterministic 320x200 column renderer, no libc, heap, floats or threads.
 * Independent host reference workload; NOT wired into Warden yet. */
#include "render.h"
#define FP 65536LL
static const char world[16][17] = {
 "################","##.............#","#..............#","#...##.........#",
 "#..............#","#.......#......#","#.......#......#","#..............#",
 "#..........#...#","#..........#...#","#..............#","#..#...........#",
 "#..#...........#","#..............#","#..............#","################"
};
static unsigned char shade(unsigned int tile, unsigned int side, unsigned int y) {
    unsigned int base = tile == '#' ? 172U : 108U;
    unsigned int v = base - side * 29U + (y % 9U);
    return (unsigned char)v;
}
void probe_render(unsigned int frame, unsigned int pixels[PROBE_WIDTH * PROBE_HEIGHT]) {
    /* All geometry and camera positions use signed 16.16 fixed point. */
    const long long ox = 3LL * FP + FP / 2;
    const long long oy = (3LL * FP + FP / 2) + ((long long)(frame % 80U) - 40LL) * 256LL;
    for (unsigned int x = 0; x < PROBE_WIDTH; ++x) {
        const long long dy = ((long long)x - (long long)(PROBE_WIDTH / 2U)) * 256LL;
        const long long ady = dy < 0 ? -dy : dy;
        int mx = (int)(ox / FP), my = (int)(oy / FP);
        const int stepy = dy < 0 ? -1 : 1;
        long long side_x = ((long long)(mx + 1) * FP - ox);
        long long side_y = ady == 0 ? 0x3fffffffffffffffLL
            : ((dy < 0 ? oy - (long long)my * FP
                         : ((long long)(my + 1) * FP - oy)) * FP) / ady;
        const long long delta_y = ady == 0 ? 0x3fffffffffffffffLL : (FP * FP) / ady;
        long long dist = FP;
        unsigned int side = 0;
        char tile = '#';
        for (unsigned int step = 0; step < 48U; ++step) {
            if (side_x < side_y) {
                dist = side_x; side_x += FP; ++mx; side = 0;
            } else {
                dist = side_y; side_y += delta_y; my += stepy; side = 1;
            }
            if (mx < 0 || mx >= 16 || my < 0 || my >= 16) break;
            tile = world[my][mx];
            if (tile != '.') break;
        }
        if (dist < 1) dist = 1;
        unsigned int height = (unsigned int)((PROBE_HEIGHT * FP) / dist);
        if (height > PROBE_HEIGHT) height = PROBE_HEIGHT;
        const unsigned int top = (PROBE_HEIGHT - height) / 2U;
        const unsigned int bottom = top + height;
        for (unsigned int y = 0; y < PROBE_HEIGHT; ++y) {
            unsigned int c;
            if (y < top) c = 0xFF203050U;
            else if (y < bottom) {
                unsigned int v = shade((unsigned int)tile, side, y);
                c = 0xFF000000U | (v << 16) | ((v / 2U) << 8) | (v / 3U);
            } else c = 0xFF303020U;
            pixels[y * PROBE_WIDTH + x] = c;
        }
    }
}
unsigned long long probe_hash(const unsigned int pixels[PROBE_WIDTH * PROBE_HEIGHT]) {
    unsigned long long h = 14695981039346656037ULL;
    for (unsigned int i = 0; i < PROBE_WIDTH * PROBE_HEIGHT; ++i) {
        unsigned int p = pixels[i];
        /* Byte order independent: little-endian canonical ARGB word digest. */
        for (unsigned int j = 0; j < 4; ++j) {
            h ^= (unsigned long long)((p >> (j * 8U)) & 255U);
            h *= 1099511628211ULL;
        }
    }
    return h;
}
