/* Independent mock negative-path tests for PR #88 Warden Doomgeneric callbacks.
 * Host only: never confuses callback tests with native Warden/QEMU Doom.
 */
#include "doomgeneric.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static pixel_t screen[DOOMGENERIC_RESX * DOOMGENERIC_RESY];
pixel_t *DG_ScreenBuffer = screen;

static uint64_t display_info = 320U | ((uint64_t)240U << 16);
static uint64_t ticks = 10;
static uint64_t scancodes[8];
static unsigned int scancode_count, scancode_index;
static int present_error, tick_error, key_error;
static unsigned int exits, exit_code, presents, yields;

uint64_t warden_doom_test_sys3(uint64_t num, uint64_t a, uint64_t b, uint64_t c) {
    (void)b;
    (void)c;
    switch (num) {
    case 5: ++yields; ++ticks; return 0;
    case 6: ++exits; exit_code = (unsigned int)a; return 0;
    case 60: assert(a == 1U); return display_info;
    case 61: assert(a == 1U); ++presents; return present_error ? UINT64_MAX : 0;
    case 62: assert(a == 1U); return key_error ? UINT64_MAX :
        (scancode_index < scancode_count ? scancodes[scancode_index++] : 0);
    case 63: assert(a == 1U); return tick_error ? UINT64_MAX : ticks;
    default: assert(0 && "unexpected syscall"); return UINT64_MAX;
    }
}

static void reset_exit(void) {
    exits = 0;
    exit_code = 0;
}

static void expect_failure(unsigned int code) {
    assert(exits == 1U);
    assert(exit_code == code);
}

int main(void) {
    /* Wrong display dimensions must never be silently accepted. */
    display_info = 319U | ((uint64_t)240U << 16);
    DG_Init();
    expect_failure(3);

    /* Successful initialization and a null framebuffer must refuse drawing. */
    reset_exit();
    display_info = 320U | ((uint64_t)240U << 16);
    DG_Init();
    assert(exits == 0U);
    DG_ScreenBuffer = NULL;
    DG_DrawFrame();
    expect_failure(4);

    /* The present syscall may deny the capability, and must be checked. */
    reset_exit();
    DG_ScreenBuffer = screen;
    present_error = 1;
    DG_DrawFrame();
    expect_failure(5);
    assert(presents == 1U);
    present_error = 0;

    /* Time failures must not become enormous positive timestamps. */
    reset_exit();
    tick_error = 1;
    assert(DG_GetTicksMs() == 0U);
    expect_failure(6);
    reset_exit();
    DG_SleepMs(5);
    expect_failure(6);
    tick_error = 0;

    /* A keyboard syscall error must not be misread as a scancode. */
    reset_exit();
    key_error = 1;
    int down = -1;
    unsigned char key = 0;
    assert(DG_GetKey(&down, &key) == 0);
    expect_failure(7);
    key_error = 0;

    /* Preserve an extended-key prefix across separate polling calls. */
    reset_exit();
    scancodes[0] = 0xe0;
    scancode_count = 1;
    scancode_index = 0;
    assert(DG_GetKey(&down, &key) == 0);
    scancodes[0] = 0x48;
    scancode_index = 0;
    assert(DG_GetKey(&down, &key) == 1);
    assert(down == 1 && key == 0xad);
    assert(exits == 0U);

    /* Zero sleep must not consume a scheduler tick. */
    unsigned int before = yields;
    DG_SleepMs(0);
    assert(yields == before);
    puts("PASS Doomgeneric adapter failure and edge-path contract");
    return 0;
}
