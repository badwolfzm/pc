/* Host-only runner. Distinguishes benchmark timing from Warden guest FPS. */
#define _POSIX_C_SOURCE 200809L
#include "render.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
static struct {
    unsigned int before;
    unsigned int pixels[PROBE_WIDTH * PROBE_HEIGHT];
    unsigned int after;
} surface;
static double elapsed(const struct timespec *a, const struct timespec *b) {
    return (double)(b->tv_sec - a->tv_sec) + (double)(b->tv_nsec - a->tv_nsec) / 1000000000.0;
}
int main(int argc, char **argv) {
    if (argc != 2 || !argv[1][0] || argv[1][0] == '-') {
        fputs("usage: game-frame-probe FRAMES (1..1000)\n", stderr); return 2;
    }
    errno = 0;
    char *end = NULL;
    unsigned long count = strtoul(argv[1], &end, 10);
    if (errno || !end || *end || count < 1UL || count > 1000UL) {
        fputs("invalid frame count\n", stderr); return 2;
    }
    surface.before = 0xD3ADB33FU; surface.after = 0xC0FFEE12U;
    struct timespec t0, t1;
    if (clock_gettime(CLOCK_MONOTONIC, &t0)) return 3;
    unsigned long long aggregate = 14695981039346656037ULL;
    for (unsigned long i = 0; i < count; ++i) {
        probe_render((unsigned int)i, surface.pixels);
        unsigned long long h = probe_hash(surface.pixels);
        aggregate ^= h;
        aggregate *= 1099511628211ULL;
    }
    if (clock_gettime(CLOCK_MONOTONIC, &t1)) return 3;
    if (surface.before != 0xD3ADB33FU || surface.after != 0xC0FFEE12U) {
        fputs("frame buffer sentinel corrupted\n", stderr); return 4;
    }
    printf("frames=%lu width=%u height=%u bytes=%zu digest=%016llx seconds=%.6f single_thread=1 host_only=1\n",
           count, PROBE_WIDTH, PROBE_HEIGHT, sizeof surface.pixels,
           aggregate, elapsed(&t0, &t1));
    return 0;
}
