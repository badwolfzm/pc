/* rt.c - freestanding libc subset and the bump heap for guest apps. The heap
 * lives in the writable pages the kernel mapped above .bss; nothing is ever
 * freed (the app's whole life is one arena, like the kernel's own heap). */
#include "rt.h"
extern char __heap_start[];
extern char __heap_end[];
static char *brk_ptr;

void *memcpy(void *d, const void *s, size_t n) {
    char *dd = d; const char *ss = s;
    while (n >= 8) { uint64_t v; __builtin_memcpy(&v, ss, 8); __builtin_memcpy(dd, &v, 8); dd += 8; ss += 8; n -= 8; }
    while (n--) *dd++ = *ss++;
    return d;
}
void *memset(void *d, int c, size_t n) { unsigned char *p = d; while (n--) *p++ = (unsigned char)c; return d; }
void *malloc(size_t n) {
    if (!brk_ptr) brk_ptr = __heap_start;
    n = (n + 63) & ~(size_t)63;
    if ((size_t)(__heap_end - brk_ptr) < n) return 0;
    void *p = brk_ptr; brk_ptr += n; return p;
}
void *calloc(size_t n, size_t sz) { size_t t = n * sz; void *p = malloc(t); if (p) memset(p, 0, t); return p; }
void free(void *p) { (void)p; }
int memcmp(const void *a, const void *b, size_t n) { const unsigned char *x = a, *y = b; for (size_t i = 0; i < n; ++i) if (x[i] != y[i]) return x[i] - y[i]; return 0; }
/* 128-bit signed division: the canvas's Bresenham uses __int128 for overflow-free slopes. */
__int128 __divti3(__int128 a, __int128 b) {
    int neg = 0; unsigned __int128 ua = (unsigned __int128)a, ub = (unsigned __int128)b;
    if (a < 0) { ua = (unsigned __int128)(-a); neg ^= 1; }
    if (b < 0) { ub = (unsigned __int128)(-b); neg ^= 1; }
    unsigned __int128 q = 0, r = 0;
    for (int i = 127; i >= 0; --i) { r = (r << 1) | ((ua >> i) & 1); if (r >= ub) { r -= ub; q |= (unsigned __int128)1 << i; } }
    return neg ? -(__int128)q : (__int128)q;
}
