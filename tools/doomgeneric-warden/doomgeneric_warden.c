/* Warden native Doomgeneric platform callbacks.
 * Original adapter (no upstream engine or WAD data vendored here).
 * Compile Doomgeneric with -DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200,
 * without CMAP256. Run only after a separately reviewed capability grant.
 */
#include <stdint.h>
#include <stddef.h>
#include "doomgeneric.h"

#if DOOMGENERIC_RESX != 320 || DOOMGENERIC_RESY != 200
#error "Warden Doomgeneric adapter currently supports 320x200 only"
#endif
#ifdef CMAP256
#error "Warden Doomgeneric adapter requires 32-bit pixels"
#endif
_Static_assert(sizeof(pixel_t) == sizeof(uint32_t), "32-bit pixels required");

enum {
    W_SYS_EXIT = 6,
    W_SYS_YIELD = 5,
    W_SYS_DISPLAY_INFO = 60,
    W_SYS_PRESENT = 61,
    W_SYS_KEY = 62,
    W_SYS_TICKS = 63,
    W_SLOT_DISPLAY = 1
};

#ifdef WARDEN_DOOM_TEST
extern uint64_t warden_doom_test_sys3(uint64_t, uint64_t, uint64_t, uint64_t);
#define wsys(n,a,b,c) warden_doom_test_sys3((n),(a),(b),(c))
#else
static inline uint64_t wsys(uint64_t n, uint64_t a, uint64_t b, uint64_t c) {
    uint64_t result;
    __asm__ volatile("int $0x80" : "=a"(result)
                     : "a"(n), "D"(a), "S"(b), "d"(c)
                     : "memory", "rcx", "r11");
    return result;
}
#endif

/* The display syscall requires a full 320x240-stride user buffer even when
 * submitting only the upper 320x200 rectangle. The extra rows stay black.
 * This buffer is statically bounded and scoped to the guest address space.
 */
static uint32_t warden_surface[320U * 240U];
static int key_extended;
static int initialized;

static void fail(uint64_t code) {
    (void)wsys(W_SYS_EXIT, code, 0, 0);
#ifndef WARDEN_DOOM_TEST
    for (;;) { __asm__ volatile("pause"); }
#endif
}

void DG_Init(void) {
    uint64_t info = wsys(W_SYS_DISPLAY_INFO, W_SLOT_DISPLAY, 0, 0);
    if ((int64_t)info < 0 || (info & 65535U) != 320U
        || ((info >> 16) & 65535U) != 240U) {
        fail(3);
        return;
    }
    for (unsigned int i = 0; i < 320U * 240U; ++i) {
        warden_surface[i] = UINT32_C(0xff000000);
    }
    key_extended = 0;
    initialized = 1;
}

void DG_DrawFrame(void) {
    if (!initialized || DG_ScreenBuffer == NULL) {
        fail(4);
        return;
    }
    for (unsigned int i = 0; i < 320U * 200U; ++i)
        warden_surface[i] = (uint32_t)DG_ScreenBuffer[i];
    /* x=0,y=0,w=320,h=200; the kernel verifies the display capability,
     * width, height and mapped guest memory before copying any pixels.
     */
    const uint64_t rect = ((uint64_t)320 << 32) | ((uint64_t)200 << 48);
    if ((int64_t)wsys(W_SYS_PRESENT, W_SLOT_DISPLAY,
                      (uint64_t)(uintptr_t)warden_surface, rect) < 0)
        fail(5);
}

uint32_t DG_GetTicksMs(void) {
    uint64_t ticks = wsys(W_SYS_TICKS, W_SLOT_DISPLAY, 0, 0);
    if ((int64_t)ticks < 0) {
        fail(6);
        return 0;
    }
    /* Warden's partition timer is 10ms. Natural uint32_t wrap is allowed. */
    return (uint32_t)(ticks * 10U);
}

void DG_SleepMs(uint32_t ms) {
    if (ms == 0)
        return;
    uint64_t begin = wsys(W_SYS_TICKS, W_SLOT_DISPLAY, 0, 0);
    if ((int64_t)begin < 0) {
        fail(6);
        return;
    }
    uint64_t ticks = ((uint64_t)ms + 9U) / 10U;
    while (1) {
        uint64_t now = wsys(W_SYS_TICKS, W_SLOT_DISPLAY, 0, 0);
        if ((int64_t)now < 0) {
            fail(6);
            return;
        }
        if ((uint64_t)(now - begin) >= ticks)
            return;
        (void)wsys(W_SYS_YIELD, 0, 0, 0);
    }
}

/* PS/2 set-1 scancodes; map the directional and gameplay controls to Doom's
 * internal key convention. Unknown events are deliberately not synthesized.
 */
static unsigned char decode(unsigned int scan, int extended) {
    if (extended) {
        switch (scan) {
        case 0x48: return 0xad; /* Doom up */
        case 0x50: return 0xaf; /* Doom down */
        case 0x4b: return 0xac; /* Doom left */
        case 0x4d: return 0xae; /* Doom right */
        case 0x1d: return 0x9d; /* right Ctrl */
        case 0x38: return 0xb8; /* right Alt */
        default: return 0;
        }
    }
    switch (scan) {
    case 0x01: return 27;
    case 0x0f: return 9;
    case 0x1c: return 13;
    case 0x39: return ' ';
    case 0x11: return 'w';
    case 0x1e: return 'a';
    case 0x1f: return 's';
    case 0x20: return 'd';
    case 0x10: return 'q';
    case 0x1d: return 0x9d;
    case 0x38: return 0xb8;
    case 0x2a: case 0x36: return 0xb6;
    case 0x48: return 0xad;
    case 0x50: return 0xaf;
    case 0x4b: return 0xac;
    case 0x4d: return 0xae;
    default: return 0;
    }
}

int DG_GetKey(int *pressed, unsigned char *key) {
    if (pressed == NULL || key == NULL)
        return 0;
    for (int attempt = 0; attempt < 16; ++attempt) {
        uint64_t raw = wsys(W_SYS_KEY, W_SLOT_DISPLAY, 0, 0);
        if ((int64_t)raw < 0) {
            fail(7);
            return 0;
        }
        if (raw == 0)
            return 0;
        if (raw == 0xe0) {
            key_extended = 1;
            continue;
        }
        unsigned int sc = (unsigned int)raw;
        int down = !(sc & 0x80U);
        unsigned char code = decode(sc & 0x7fU, key_extended);
        key_extended = 0;
        if (code != 0) {
            *pressed = down;
            *key = code;
            return 1;
        }
    }
    return 0;
}

void DG_SetWindowTitle(const char *title) {
    /* No window manager in native Warden; no privilege or host title access. */
    (void)title;
}
