/* Host-only link smoke, intentionally does not call Doom's engine startup.
 * No actual Doom game/WAD is run. Test validates one linker image is complete
 * and Warden's platform and file callback surface is present.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "doomgeneric.h"
#include "doom_wad_bridge.h"
uint64_t warden_doom_test_sys3(uint64_t n, uint64_t a, uint64_t b, uint64_t c) {
    (void)b; (void)c;
    switch(n) {
        case 60: return (uint64_t)320 | ((uint64_t)240 << 16);
        case 63: return 0;
        case 62: return 0;
        case 61: return 0;
        case 5: return 0;
        case 6: return 0;
        default: return UINT64_MAX;
    }
    (void)a;
}
int main(void) {
    int pressed = 0;
    unsigned char key = 0;
    DG_Init();
    if (DG_GetTicksMs() != 0 || DG_GetKey(&pressed, &key) != 0) return 2;
    if (warden_doom_wad_class.OpenFile("DOOM1.WAD") != NULL) return 3;
    puts("PASS: real upstream Doom objects and Warden callbacks linked (host-only)");
    return 0;
}
