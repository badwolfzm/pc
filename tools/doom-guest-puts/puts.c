/* Freestanding Warden Doom puts. No host libc/stdout access.
 * The trusted guest bootstrap must install an explicitly authorized writer.
 * This adapter alone is NOT a capability boundary or guest syscall binding.
 */
#include <stddef.h>

enum { MAX_LINE = 1023, CHUNK = 128 };

typedef int (*warden_console_writer)(const char *bytes, size_t n, void *ctx);
static warden_console_writer console_writer;
static void *console_ctx;

/* Setting NULL revokes access. The trusted bootstrap owns pointer lifetime.
 * NOT thread-safe: installation is only allowed before guest game execution.
 */
void warden_doom_set_console_writer(warden_console_writer writer, void *ctx) {
    console_writer = writer;
    console_ctx = writer ? ctx : (void *)0;
}

int puts(const char *s) {
    if (!console_writer || !s) return -1;
    size_t length = 0;
    while (length <= MAX_LINE && s[length] != '\0') ++length;
    if (length > MAX_LINE) return -1; /* no unbounded scan */
    size_t sent = 0;
    while (sent < length) {
        size_t n = length - sent;
        if (n > CHUNK) n = CHUNK;
        if (console_writer(s + sent, n, console_ctx) != (int)n) return -1;
        sent += n;
    }
    const char nl = '\n';
    if (console_writer(&nl, 1, console_ctx) != 1) return -1;
    return 0; /* nonnegative success, EOF-style -1 on failure */
}
