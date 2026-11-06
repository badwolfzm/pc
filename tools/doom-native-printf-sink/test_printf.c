#include <assert.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

typedef int (*warden_sink_fn)(const char *, size_t, void *);
void warden_printf_bind(warden_sink_fn, void *);
static char captured[5000];
static size_t captured_len;
static int requested_failure;

static int capture(const char *data, size_t len, void *ctx) {
    assert(ctx == captured);
    assert(len < sizeof(captured));
    if (requested_failure) return (int)len - 1;
    memcpy(captured, data, len);
    captured_len = len;
    return (int)len;
}
int main(void) {
    int (*volatile call)(const char *, ...) = printf;
    assert(call("unbound %d", 1) == -1);
    warden_printf_bind(capture, captured);
    assert(call("doom %d %x", -7, 255u) == 10);
    assert(captured_len == 10 && memcmp(captured, "doom -7 ff", 10) == 0);
    assert(call("") == 0);
    assert(captured_len == 10);
    requested_failure = 1;
    assert(call("x") == -1);
    requested_failure = 0;
    char large[4200];
    memset(large, 'a', sizeof(large) - 1);
    large[sizeof(large) - 1] = 0;
    assert(call("%s", large) == -1);
    assert(call("end%") == -1); /* merged formatter rejects bad spec */
    warden_printf_bind(NULL, captured);
    assert(call("off") == -1);
    return 0;
}
