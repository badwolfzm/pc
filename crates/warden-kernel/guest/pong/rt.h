/* rt.h - the MatrixOS guest runtime for Warden ring-3 apps (freestanding).
 * Syscalls are `int 0x80` with rax = number, rdi/rsi/rdx/r10 = args, rax = result.
 * Numbers 60..64 are the display module (crates/warden-kernel/src/pong.rs). */
#ifndef WARDEN_RT_H
#define WARDEN_RT_H
#include <stdint.h>
#include <stddef.h>

#define SYS_YIELD        5
#define SYS_EXIT         6
#define SYS_DISPLAY_INFO 60   /* (display_slot) -> w | h<<16 */
#define SYS_PRESENT      61   /* (display_slot, px_base, x|y<<16|w<<32|h<<48) -> 0; copies the rect of a w_display-stride buffer */
#define SYS_KEY          62   /* (display_slot) -> next PS/2 scancode or 0 */
#define SYS_TICKS        63   /* (display_slot) -> partition timer ticks (10 ms) */
#define SYS_REPORT       64   /* (console_slot, key, value) -> 0; printed on the kernel console */

#define SLOT_CONSOLE 0
#define SLOT_DISPLAY 1

static inline uint64_t sys3(uint64_t n, uint64_t a, uint64_t b, uint64_t c) {
    uint64_t r;
    __asm__ volatile ("int $0x80" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c) : "memory", "rcx", "r11");
    return r;
}
static inline uint64_t rdtsc(void) { uint32_t lo, hi; __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi)); return ((uint64_t)hi << 32) | lo; }

void *memcpy(void *d, const void *s, size_t n);
void *memset(void *d, int c, size_t n);
int   memcmp(const void *a, const void *b, size_t n);
void *calloc(size_t n, size_t sz);
void *malloc(size_t n);
void  free(void *p);
#endif
