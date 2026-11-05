/* Warden native ring-3 adapter for the independent 320x200 game-frame-probe.
 * Uses the capability-gated Pong display ABI; NOT linked into kernel boot yet.
 * A 320x240 presentation buffer is required by SYS_PRESENT's fixed stride.
 */
#include "../../crates/warden-kernel/guest/pong/rt.h"
#include "../game-frame-probe/render.h"

#define DISPLAY_W 320u
#define DISPLAY_H 240u
#define SOURCE_H 200u
#define NFRAMES 300u

_Static_assert(PROBE_WIDTH == DISPLAY_W, "display stride mismatch");
_Static_assert(PROBE_HEIGHT == SOURCE_H, "source height mismatch");
_Static_assert(sizeof(unsigned int) == 4, "ARGB32 required");

/* 307,200 B in guest .bss, within Pong's 0x840000..0x9e0000 mapping. */
static unsigned int screen[DISPLAY_W * DISPLAY_H];

static uint64_t rect(unsigned x, unsigned y, unsigned w, unsigned h) {
    return (uint64_t)x | ((uint64_t)y << 16) |
           ((uint64_t)w << 32) | ((uint64_t)h << 48);
}

int pong_main(void) {
    const uint64_t info = sys3(SYS_DISPLAY_INFO, SLOT_DISPLAY, 0, 0);
    if (info != (DISPLAY_W | ((uint64_t)DISPLAY_H << 16))) {
        (void)sys3(SYS_REPORT, SLOT_CONSOLE, 310, info);
        return 10;
    }
    /* The lower 40 rows stay opaque black, not uninitialized pixels. */
    for (unsigned i = DISPLAY_W * SOURCE_H; i < DISPLAY_W * DISPLAY_H; ++i)
        screen[i] = 0xff000000u;

    uint64_t transcript = UINT64_C(14695981039346656037);
    const uint64_t full = rect(0, 0, DISPLAY_W, DISPLAY_H);
    for (unsigned frame = 0; frame < NFRAMES; ++frame) {
        probe_render(frame, screen); /* exactly 320x200 at the start of screen */
        uint64_t digest = (uint64_t)probe_hash(screen);
        transcript = (transcript ^ digest) * UINT64_C(1099511628211);
        const uint64_t status = sys3(SYS_PRESENT, SLOT_DISPLAY, (uint64_t)(uintptr_t)screen, full);
        if (status != 0) {
            (void)sys3(SYS_REPORT, SLOT_CONSOLE, 311, status);
            return 11;
        }
        (void)sys3(SYS_YIELD, 0, 0, 0);
    }
    (void)sys3(SYS_REPORT, SLOT_CONSOLE, 312, NFRAMES);
    (void)sys3(SYS_REPORT, SLOT_CONSOLE, 313, transcript);
    return 0;
}
