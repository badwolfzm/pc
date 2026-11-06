#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
/* Actual adapter linked as renamed symbol for safe host test execution. */
extern int warden_guest_puts(const char *);
typedef int (*writer)(const char *, size_t, void *);
extern void warden_doom_set_console_writer(writer, void *);
struct buffer { char bytes[2048]; size_t n; int calls; int fail_call; };
static int capture(const char *src, size_t n, void *ctx) {
    struct buffer *b = ctx;
    b->calls++;
    if (b->calls == b->fail_call) return -1;
    if (b->n + n > sizeof b->bytes) return -1;
    memcpy(b->bytes + b->n, src, n);
    b->n += n;
    return (int)n;
}
int main(void) {
    struct buffer b = {{0},0,0,0};
    warden_doom_set_console_writer(NULL, NULL);
    assert(warden_guest_puts("denied") == -1);
    warden_doom_set_console_writer(capture, &b);
    assert(warden_guest_puts(NULL) == -1);
    assert(warden_guest_puts("") == 0);
    assert(b.n == 1 && b.bytes[0] == '\n');
    b.n = 0; b.calls = 0;
    assert(warden_guest_puts("doom") == 0);
    assert(b.n == 5 && memcmp(b.bytes,"doom\n",5) == 0);
    char max[1025]; memset(max,'a',sizeof max); max[1023] = '\0';
    b.n = 0; b.calls = 0;
    assert(warden_guest_puts(max) == 0);
    assert(b.n == 1024 && b.bytes[1023] == '\n' && b.calls >= 9);
    max[1023] = 'a'; max[1024] = '\0';
    b.n = 0; b.calls = 0;
    assert(warden_guest_puts(max) == -1 && b.calls == 0 && b.n == 0);
    b.fail_call = 1; b.calls = 0;
    assert(warden_guest_puts("denied") == -1 && b.calls == 1);
    b.fail_call = 0;
    warden_doom_set_console_writer(NULL,NULL);
    assert(warden_guest_puts("revoked") == -1);
    fputs("PASS bounded Warden puts adapter\n",stdout);
    return 0;
}
