/* Independent invariant tests for PR #68's host-side renderer.
 * This does NOT demonstrate Warden guest execution. */
#include "../../tools/game-frame-probe/render.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { PIXELS = PROBE_WIDTH * PROBE_HEIGHT };
static struct { uint32_t before; unsigned int pixels[PIXELS]; uint32_t after; } a, b;

static void guards(const uint32_t before, const uint32_t after) {
    assert(before == UINT32_C(0xa7c4b1d8));
    assert(after == UINT32_C(0x9e2503fa));
}
static void init(void) {
    memset(&a, 0, sizeof a);
    memset(&b, 0, sizeof b);
    a.before = b.before = UINT32_C(0xa7c4b1d8);
    a.after = b.after = UINT32_C(0x9e2503fa);
}
int main(void) {
    init();
    /* An independent byte-order/hash oracle, not a self-comparison only. */
    assert(probe_hash(a.pixels) == UINT64_C(0x03702d99714c4325));
    a.pixels[0] = UINT32_C(0x11223344);
    assert(probe_hash(a.pixels) == UINT64_C(0xfedad243161e55e5));
    a.pixels[0] = 0;

    for (unsigned int frame = 0; frame < 240; ++frame) {
        probe_render(frame, a.pixels);
        probe_render(frame, b.pixels);
        assert(memcmp(a.pixels, b.pixels, sizeof a.pixels) == 0);
        assert(probe_hash(a.pixels) == probe_hash(b.pixels));
        guards(a.before, a.after);
        guards(b.before, b.after);
    }
    /* Current scene camera is frame modulo 80, even across UINT_MAX. */
    for (unsigned int frame = 0; frame < 80; ++frame) {
        probe_render(frame, a.pixels);
        probe_render(frame + 80u, b.pixels);
        assert(memcmp(a.pixels, b.pixels, sizeof a.pixels) == 0);
    }
    probe_render(0u, a.pixels);
    probe_render(1u, b.pixels);
    assert(memcmp(a.pixels, b.pixels, sizeof a.pixels) != 0);
    probe_render(UINT32_MAX, a.pixels);
    guards(a.before, a.after);
    puts("PASS: canonical hash vectors, 240-frame deterministic replay, 80-frame period, frame transition, canaries");
    return 0;
}
