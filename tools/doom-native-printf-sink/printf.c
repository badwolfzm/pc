/* Warden Doom printf shim: no host stdout, host syscalls or FILE objects.
 * This is a guest-local opt-in adapter; kernel must validate any transport.
 */
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

#define WARDEN_PRINTF_BUFFER_BYTES 4096u

typedef int (*warden_sink_fn)(const char *, size_t, void *);
static warden_sink_fn bound_sink;
static void *bound_context;

/* Called by the trusted guest runtime before using printf.
 * Not a capability grant; the actual callback must enforce Warden rights.
 * NULL disables output. This single-threaded prototype is not thread safe.
 */
void warden_printf_bind(warden_sink_fn sink, void *context) {
    bound_sink = sink;
    bound_context = sink ? context : (void *)0;
}

int printf(const char *restrict format, ...) {
    char buffer[WARDEN_PRINTF_BUFFER_BYTES];
    va_list ap;
    int count;
    int wrote;
    warden_sink_fn sink = bound_sink;
    if (!sink || !format) return -1;
    va_start(ap, format);
    count = vsnprintf(buffer, sizeof(buffer), format, ap);
    va_end(ap);
    if (count < 0 || (size_t)count >= sizeof(buffer)) return -1;
    if (count == 0) return 0;
    wrote = sink(buffer, (size_t)count, bound_context);
    if (wrote != count) return -1;
    return count;
}
