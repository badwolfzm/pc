/* Warden native Doom read-only serial output adapter.
 * This is NOT a generic libc/stdio implementation. The existing kernel
 * checks endpoint identity + WRITE rights for SYS_CONSOLE_WRITE on slot 0.
 * Caller must hold a separately granted console capability in slot 0.
 * Never use SYS_HOST_EXIT or a Linux host I/O API.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
/* Signature imported from the selected freestanding formatter, not host stdio. */
extern int vsnprintf(char *restrict, size_t, const char *restrict, va_list);

enum {
    DOOM_WARDEN_CONSOLE_WRITE = 13,
    DOOM_WARDEN_CONSOLE_SLOT = 0,
    DOOM_WARDEN_CONSOLE_MAX = 1024
};

#ifdef WARDEN_DOOM_CONSOLE_TEST
extern long warden_doom_console_test_syscall(unsigned long,
                                            unsigned long,
                                            unsigned long,
                                            unsigned long);
static long do_write(const void *data, size_t n) {
    return warden_doom_console_test_syscall(
        DOOM_WARDEN_CONSOLE_WRITE, DOOM_WARDEN_CONSOLE_SLOT,
        (unsigned long)(uintptr_t)data, (unsigned long)n);
}
#else
static long do_write(const void *data, size_t n) {
    long result;
    __asm__ volatile("int $0x80"
                     : "=a"(result)
                     : "a"((unsigned long)DOOM_WARDEN_CONSOLE_WRITE),
                       "D"((unsigned long)DOOM_WARDEN_CONSOLE_SLOT),
                       "S"((unsigned long)(uintptr_t)data),
                       "d"((unsigned long)n)
                     : "memory", "rcx", "r11");
    return result;
}
#endif

static int bounded_console_write(const char *bytes, size_t len) {
    if (!bytes || !len || len > DOOM_WARDEN_CONSOLE_MAX)
        return -1;
    long result = do_write(bytes, len);
    /* No silent short writes, and no retry into another guest grant. */
    if (result < 0 || (unsigned long)result != (unsigned long)len)
        return -1;
    return (int)len;
}

int printf(const char *restrict format, ...) {
    if (!format) return -1;
    char bytes[DOOM_WARDEN_CONSOLE_MAX];
    va_list args;
    va_start(args, format);
    int len = vsnprintf(bytes, sizeof(bytes), format, args);
    va_end(args);
    /* Reject formatting errors and truncation BEFORE a console syscall. */
    if (len < 0 || (unsigned int)len >= sizeof(bytes)) return -1;
    if (len == 0) return 0;
    return bounded_console_write(bytes, (size_t)len);
}

int puts(const char *s) {
    if (!s) return -1;
    char bytes[DOOM_WARDEN_CONSOLE_MAX];
    size_t n = 0;
    /* 1023 content bytes + exactly one newline fit the syscall limit. */
    while (n < DOOM_WARDEN_CONSOLE_MAX - 1 && s[n]) {
        bytes[n] = s[n];
        ++n;
    }
    if (s[n] != '\0') return -1;
    bytes[n++] = '\n';
    return bounded_console_write(bytes, n) < 0 ? -1 : 0;
}
