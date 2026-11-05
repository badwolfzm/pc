/* Visible periodic camera motion regression for the deterministic G0 renderer.
 * Host-only check; native guest evidence is separately required.
 */
#include "render.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static unsigned int a[PROBE_WIDTH * PROBE_HEIGHT];
static unsigned int b[PROBE_WIDTH * PROBE_HEIGHT];

int main(void) {
    probe_render(0u, a);
    probe_render(40u, b);
    if (memcmp(a, b, sizeof a) == 0) {
        fputs("FAIL: camera moved in input but produced an identical frame\n", stderr);
        return 1;
    }
    probe_render(80u, b);
    if (memcmp(a, b, sizeof a) != 0) {
        fputs("FAIL: frame 80 does not repeat frame 0\n", stderr);
        return 1;
    }
    puts("PASS: camera motion visible and periodic");
    return 0;
}
