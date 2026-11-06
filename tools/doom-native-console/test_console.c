/* Run with the actual merged bounded formatter under renamed symbols and
 * inject the native console syscall. This makes permission denials observable.
 */
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <limits.h>
#include <string.h>
#include <stdio.h>

extern int warden_test_printf(const char *, ...);
extern int warden_test_puts(const char *);

static unsigned char seen[1024];
static int calls;
static size_t bytes_written;
static int mode; /* 0 success, 1 permission refused, 2 short write */
long warden_doom_console_test_syscall(unsigned long sys,
                                     unsigned long slot,
                                     unsigned long ptr,
                                     unsigned long n) {
    assert(sys == 13UL);
    assert(slot == 0UL);
    assert(ptr != 0);
    assert(n > 0 && n <= sizeof(seen));
    ++calls;
    bytes_written = n;
    memcpy(seen, (const void *)(uintptr_t)ptr, n);
    if (mode == 1) return -3;  /* InsufficientRights */
    if (mode == 2) return (long)n - 1;
    return (long)n;
}
static void reset(void) {
    calls = 0; bytes_written = 0; mode = 0;
    memset(seen, 0, sizeof seen);
}
int main(void) {
    reset();
    assert(warden_test_printf("hi %d %s", 42, "ok") == 8);
    assert(calls == 1 && bytes_written == 8);
    assert(memcmp(seen, "hi 42 ok", 8) == 0);
    reset();
    assert(warden_test_printf("") == 0 && calls == 0);
    reset();
    assert(warden_test_puts("hello") == 0);
    assert(calls == 1 && bytes_written == 6);
    assert(memcmp(seen, "hello\n", 6) == 0);
    reset();
    assert(warden_test_puts("") == 0);
    assert(calls == 1 && seen[0] == '\n');
    char large[1026];
    for (int i = 0; i < 1025; i++) large[i] = 'Q';
    large[1023] = '\0';
    reset();
    assert(warden_test_printf("%s", large) == 1023);
    assert(calls == 1 && bytes_written == 1023);
    reset();
    assert(warden_test_puts(large) == 0);
    assert(calls == 1 && bytes_written == 1024 && seen[1023] == '\n');
    large[1023] = 'Q';
    large[1024] = '\0';
    reset();
    assert(warden_test_printf("%s", large) == -1 && calls == 0);
    assert(warden_test_puts(large) == -1 && calls == 0);
    reset();
    assert(warden_test_printf("%p", (void *)0) == -1 && calls == 0);
    assert(warden_test_printf("%f", 3.5) == -1 && calls == 0);
    assert(warden_test_printf("%n", (int *)0) == -1 && calls == 0);
    assert(warden_test_printf("%99999999999d", 1) == -1 && calls == 0);
    assert(warden_test_printf("%*d", INT_MIN, 7) == -1 && calls == 0);
    reset(); mode = 1;
    assert(warden_test_printf("access refused") == -1 && calls == 1);
    reset(); mode = 2;
    assert(warden_test_puts("short") == -1 && calls == 1);
    reset();
    assert(warden_test_printf("%s", (const char *)0) == -1 && calls == 0);
    puts("PASS native Doom console capability boundary, max bytes, failures");
    return 0;
}
