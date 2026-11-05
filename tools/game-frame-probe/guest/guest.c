/* Warden native ring-3 display adapter for the host-verified renderer.
 * Buildable as an independent freestanding image; NOT integrated into boot.
 * Uses already defined Pong feature syscalls, with no new kernel ABI.
 */
#include "../render.h"
typedef unsigned long long u64;
#define SLOT_DISPLAY 1ULL
#define SLOT_CONSOLE 0ULL
#define SYS_YIELD 5ULL
#define SYS_DISPLAY_INFO 60ULL
#define SYS_PRESENT 61ULL
#define SYS_KEY 62ULL
#define SYS_REPORT 64ULL

static unsigned int framebuffer[320U * 240U];
static u64 call(u64 nr, u64 a, u64 b, u64 c) {
    u64 result;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(nr), "D"(a), "S"(b), "d"(c) : "memory", "rcx", "r11");
    return result;
}
static int failed(u64 result) { return (long long)result < 0; }
/* Mirror the kernel's full 320x240 ARGB framebuffer digest. Only 320x200
 * was submitted, so the remaining 40 rows retain the kernel's black clear.
 * Deliberately independent of probe_hash(), which covers only rendered rows.
 */
static u64 expected_display_hash(void) {
    u64 h = 1469598103934665603ULL;
    for (unsigned int i = 0; i < 320U * 240U; ++i) {
        unsigned int v = i < 320U * 200U ? framebuffer[i] : 0xff000000U;
        for (unsigned int j = 0; j < 4U; ++j) {
            h ^= (u64)((v >> (j * 8U)) & 255U);
            h *= 1099511628211ULL;
        }
    }
    return h;
}
int game_probe_main(void) {
    /* Existing device contract: a granted 320x240 display; renderer draws
     * only 320x200 within the start of the larger stride-matched buffer. */
    u64 info = call(SYS_DISPLAY_INFO, SLOT_DISPLAY, 0, 0);
    if (failed(info) || (info & 65535ULL) != 320ULL ||
        ((info >> 16) & 65535ULL) != 240ULL) return 3;
    u64 digest = 14695981039346656037ULL;
    unsigned int completed = 0;
    for (unsigned int i = 0; i < 60U; ++i) {
        u64 key = call(SYS_KEY, SLOT_DISPLAY, 0, 0);
        if ((key & 127ULL) == 0x10ULL && (key & 128ULL) == 0) break;
        probe_render(i, framebuffer);
        digest ^= probe_hash(framebuffer);
        digest *= 1099511628211ULL;
        /* Present only the 320x200 region; kernel validates capability,
         * mapped pointer and dimensions before copying into framebuffer. */
        u64 rect = 320ULL << 32 | 200ULL << 48;
        if (failed(call(SYS_PRESENT, SLOT_DISPLAY, (u64)(unsigned long)framebuffer, rect))) return 4;
        ++completed;
        call(SYS_YIELD, 0, 0, 0);
    }
    call(SYS_REPORT, SLOT_CONSOLE, 40, completed);
    call(SYS_REPORT, SLOT_CONSOLE, 41, digest);
    call(SYS_REPORT, SLOT_CONSOLE, 42, expected_display_hash());
    return completed == 60U ? 0 : 5;
}
