/* Narrow read-only Doom stdio ABI, backed ONLY by an explicit grant.
 * No libc/host filesystem, no Warden-grant minting and no write mode.
 * Opaque FILE* is used solely as an identity token, never dereferenced.
 */
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <limits.h>

#define MAX_BYTES (64u * 1024u * 1024u)
#define MAX_HANDLES 16u
/* Callback returns 0 on a complete read, nonzero on failure. */
typedef int (*warden_read_at)(void *, uint32_t, void *, size_t);
struct slot { uint32_t cursor; unsigned char opened; };
static struct slot slots[MAX_HANDLES];
static unsigned int ever_opened;
static char permitted[256];
static uint32_t source_length;
static void *source_ctx;
static warden_read_at source_read;
static unsigned char configured;
static unsigned char revoked;

/* One grant per image lifetime; prevents invalidated opaque FILE* reuse. */
int warden_stdio_grant_readonly(const char *name, void *ctx,
                              warden_read_at reader, uint32_t size) {
    if (configured || revoked || !name || !reader || size > MAX_BYTES)
        return -1;
    size_t i = 0;
    while (i < sizeof permitted - 1 && name[i]) {
        if (name[i] == '\\' || (name[i] == '.' && name[i+1] == '.'))
            return -1;
        permitted[i] = name[i];
        i++;
    }
    if (!i || name[i] != 0 || name[0] == '/') return -1;
    permitted[i] = 0;
    source_ctx = ctx;
    source_read = reader;
    source_length = size;
    configured = 1;
    return 0;
}
void warden_stdio_revoke(void) {
    configured = 0;
    revoked = 1;
    source_ctx = 0;
    source_read = 0;
    for (unsigned int i = 0; i < MAX_HANDLES; ++i) slots[i].opened = 0;
}
static int exact(const char *a, const char *b) {
    if (!a || !b) return 0;
    for (size_t n = 0; n < 256; ++n) {
        if (a[n] != b[n]) return 0;
        if (a[n] == 0) return 1;
    }
    return 0;
}
static struct slot *valid(FILE *stream) {
    /* Compare opaque pointer values without dereferencing caller addresses. */
    for (unsigned i = 0; i < ever_opened; ++i)
        if ((void *)stream == (void *)&slots[i] && slots[i].opened && configured)
            return &slots[i];
    return 0;
}
FILE *fopen(const char *name, const char *mode) {
    if (!configured || !mode || !(exact(mode, "rb") || exact(mode, "r"))
        || !exact(name, permitted) || ever_opened == MAX_HANDLES)
        return 0;
    struct slot *s = &slots[ever_opened++];
    s->cursor = 0;
    s->opened = 1;
    return (FILE *)(void *)s;
}
int fclose(FILE *stream) {
    struct slot *s = valid(stream);
    if (!s) return EOF;
    s->opened = 0;
    return 0;
}
size_t fread(void *dst, size_t size, size_t count, FILE *stream) {
    struct slot *s = valid(stream);
    if (!s) return 0;
    if (!size || !count) return 0;
    if (!dst || count > SIZE_MAX / size || !source_read) return 0;
    size_t requested = count * size;
    uint32_t available = source_length - s->cursor;
    size_t bytes = requested < available ? requested : available;
    if (!bytes) return 0;
    if (source_read(source_ctx, s->cursor, dst, bytes) != 0) return 0;
    s->cursor += (uint32_t)bytes;
    return bytes / size;
}
int fseek(FILE *stream, long offset, int origin) {
    struct slot *s = valid(stream);
    if (!s) return -1;
    int64_t base;
    if (origin == SEEK_SET) base = 0;
    else if (origin == SEEK_CUR) base = s->cursor;
    else if (origin == SEEK_END) base = source_length;
    else return -1;
    /* Avoid signed addition overflow for arbitrary long values. */
    if (offset > 0 && base > INT64_MAX - (int64_t)offset) return -1;
    if (offset < 0 && base < INT64_MIN - (int64_t)offset) return -1;
    int64_t pos = base + (int64_t)offset;
    if (pos < 0 || pos > source_length) return -1;
    s->cursor = (uint32_t)pos;
    return 0;
}
long ftell(FILE *stream) {
    struct slot *s = valid(stream);
    return s ? (long)s->cursor : -1L;
}
