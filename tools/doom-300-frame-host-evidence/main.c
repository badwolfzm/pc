/* Real pinned Doomgeneric + lawful Freedoom host-frame evidence.
 * 300 FRAME records are produced only by real DG_DrawFrame callbacks.
 * Host-only using glibc/stdio; NOT Warden kernel or measured native FPS.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "doomgeneric.h"

enum { REQUIRED_FRAMES = 300 };
static uint64_t started_ms;
static unsigned frames;
static uint64_t mono_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        perror("clock_gettime");
        exit(4);
    }
    return (uint64_t)ts.tv_sec * UINT64_C(1000) +
           (uint64_t)ts.tv_nsec / UINT64_C(1000000);
}
void DG_Init(void) { started_ms = mono_ms(); }
void DG_DrawFrame(void) {
    if (!DG_ScreenBuffer) {
        fputs("FATAL: null renderer framebuffer\n", stderr);
        exit(5);
    }
    uint64_t hash = UINT64_C(1469598103934665603);
    for (unsigned i = 0; i < DOOMGENERIC_RESX * DOOMGENERIC_RESY; ++i) {
        uint32_t pixel = DG_ScreenBuffer[i];
        for (unsigned j = 0; j < 4; ++j) {
            hash ^= (pixel >> (j * 8u)) & 255u;
            hash *= UINT64_C(1099511628211);
        }
    }
    printf("FRAME,%u,%" PRIu64 ",%016" PRIx64 "\n",
           frames, mono_ms() - started_ms, hash);
    ++frames;
}
void DG_SleepMs(uint32_t ms) {
    struct timespec req = { (time_t)(ms / 1000u), (long)(ms % 1000u) * 1000000L };
    while (nanosleep(&req, &req) != 0) {
        if (errno != EINTR) { perror("nanosleep"); exit(6); }
    }
}
uint32_t DG_GetTicksMs(void) { return (uint32_t)(mono_ms() - started_ms); }
int DG_GetKey(int *pressed, unsigned char *key) {
    if (pressed) *pressed = 0;
    if (key) *key = 0;
    return 0;
}
void DG_SetWindowTitle(const char *title) { (void)title; }
int main(int argc, char **argv) {
    if (argc != 2) {
        fputs("usage: doom-300-frame /authorized/freedoom1.wad\n", stderr);
        return 2;
    }
    char *engine_argv[] = {
        "doom-300-frame", "-iwad", argv[1],
        "-nosound", "-nomusic", "-skill", "1",
        "-warp", "1", "1", NULL
    };
    doomgeneric_Create(10, engine_argv);
    /* One genuine Doom tick/present per iteration. Deliberately paced at
       approximately 35 Hz; DO NOT present this as an uncapped benchmark. */
    /* Upstream D_DoomMain can present during Create before returning.
       Stop at an exact real framebuffer callback count, not tick count.
       The attempt cap detects a stuck rendering path. */
    unsigned attempts = 0;
    while (frames < REQUIRED_FRAMES && attempts < REQUIRED_FRAMES + 30u) {
        doomgeneric_Tick();
        DG_SleepMs(28);
        ++attempts;
    }
    printf("FRAME_COUNT,%u\n", frames);
    return frames == REQUIRED_FRAMES ? 0 : 8;
}
