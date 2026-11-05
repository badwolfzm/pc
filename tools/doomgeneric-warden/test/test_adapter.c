/* Host mocks verify native Warden Doomgeneric callback behavior without
 * loading the GPL engine or game data. Not a Warden guest execution test.
 */
#include "doomgeneric.h"
#include <assert.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>

static pixel_t screen[320U * 200U];
pixel_t *DG_ScreenBuffer = screen;
static uint64_t current_ticks;
static unsigned int exit_requests;
static unsigned int presents;
static unsigned int yields;
static uint64_t last_rect;
static uint64_t display_info = (uint64_t)320 | ((uint64_t)240 << 16);
static const uint8_t *keys;
static size_t key_count, key_index;

uint64_t warden_doom_test_sys3(uint64_t num, uint64_t a, uint64_t b, uint64_t c) {
    switch (num) {
    case 5: ++yields; ++current_ticks; return 0;
    case 6: ++exit_requests; return 0;
    case 60: assert(a == 1); return display_info;
    case 61: {
        assert(a == 1);
        ++presents;
        last_rect = c;
        const uint32_t *surface = (const uint32_t *)(uintptr_t)b;
        assert(surface[0] == screen[0]);
        assert(surface[320U * 199U + 319U] == screen[320U * 199U + 319U]);
        assert(surface[320U * 200U] == UINT32_C(0xff000000));
        assert(surface[320U * 240U - 1U] == UINT32_C(0xff000000));
        return 0;
    }
    case 62: return key_index < key_count ? keys[key_index++] : 0;
    case 63: assert(a == 1); return current_ticks;
    default: assert(!"unexpected syscall"); return UINT64_MAX;
    }
}

static void set_keys(const uint8_t *data, size_t n) {
    keys = data;
    key_count = n;
    key_index = 0;
}

int main(void) {
    screen[0] = UINT32_C(0xff123456);
    screen[320U * 200U - 1U] = UINT32_C(0xffabcdef);
    DG_Init();
    DG_DrawFrame();
    assert(presents == 1 && exit_requests == 0);
    assert(last_rect == (((uint64_t)320 << 32) | ((uint64_t)200 << 48)));
    screen[0] ^= 1U;
    DG_DrawFrame();
    assert(presents == 2);
    current_ticks = 7;
    assert(DG_GetTicksMs() == 70U);
    DG_SleepMs(25U);
    assert(current_ticks == 10U && yields == 3U);
    DG_SleepMs(0);
    assert(yields == 3U);
    const uint8_t events[] = {0xe0, 0x48, 0xe0, 0xc8, 0x39, 0xb9};
    set_keys(events, sizeof events);
    int pressed = -1;
    unsigned char key = 0;
    assert(DG_GetKey(&pressed, &key) == 1 && pressed == 1 && key == 0xad);
    assert(DG_GetKey(&pressed, &key) == 1 && pressed == 0 && key == 0xad);
    assert(DG_GetKey(&pressed, &key) == 1 && pressed == 1 && key == ' ');
    assert(DG_GetKey(&pressed, &key) == 1 && pressed == 0 && key == ' ');
    assert(DG_GetKey(&pressed, &key) == 0);
    DG_SetWindowTitle("Doom");
    assert(exit_requests == 0);
    puts("PASS: Doomgeneric native display, time, input and no unexpected exits");
    return 0;
}
