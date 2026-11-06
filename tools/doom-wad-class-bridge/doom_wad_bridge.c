#include "doom_wad_bridge.h"
#include <string.h>

/* Explicitly single-open; avoids heap, host file access and opaque handles.
   A future multi-WAD port must use separately accounted slots. */
#define BRIDGE_PATH_MAX 128u
static struct {
    char path[BRIDGE_PATH_MAX];
    void *ctx;
    wad_read_at reader;
    uint32_t bytes;
    uint32_t generation;
    unsigned int granted;
} grant;
static struct {
    wad_file_t file;
    wad_stream stream;
    uint32_t generation;
    unsigned int active;
} opened;

void warden_doom_wad_revoke(void) {
    /* Fail closed: no subsequent reads after revocation. */
    grant.granted = 0;
    grant.ctx = NULL;
    grant.reader = NULL;
    grant.bytes = 0;
    memset(grant.path, 0, sizeof grant.path);
    ++grant.generation;
    opened.active = 0;
    wad_close(&opened.stream);
}

int warden_doom_wad_grant(const char *path, void *ctx, wad_read_at reader,
                         uint32_t source_bytes) {
    warden_doom_wad_revoke();
    if (!path || !reader || source_bytes < 12 || source_bytes > WAD_MAX_SIZE)
        return 0;
    size_t n = 0;
    while (n < BRIDGE_PATH_MAX && path[n] != '\0') ++n;
    if (n == 0 || n == BRIDGE_PATH_MAX) return 0;
    /* Never grant dot, parent-relative or path traversal aliases. */
    if (path[0] == '/' || path[0] == '.' || strchr(path, '/') ||
        strchr(path, '\\')) return 0;
    memcpy(grant.path, path, n + 1);
    grant.ctx = ctx;
    grant.reader = reader;
    grant.bytes = source_bytes;
    grant.granted = 1;
    return 1;
}

static wad_file_t *bridge_open(char *path) {
    if (!path || !grant.granted || opened.active ||
        strcmp(path, grant.path) != 0) return NULL;
    if (!wad_open(&opened.stream, grant.ctx, grant.reader, grant.bytes))
        return NULL;
    opened.file.file_class = &warden_doom_wad_class;
    opened.file.mapped = NULL;
    opened.file.length = grant.bytes;
    opened.generation = grant.generation;
    opened.active = 1;
    return &opened.file;
}

static void bridge_close(wad_file_t *f) {
    if (!opened.active || f != &opened.file) return;
    opened.active = 0;
    wad_close(&opened.stream);
}

static size_t bridge_read(wad_file_t *f, unsigned int off, void *dst,
                          size_t len) {
    if (!opened.active || f != &opened.file || !grant.granted ||
        opened.generation != grant.generation || (!dst && len)) return 0;
    if (len == 0) return 0;
    if (off > opened.stream.size || len > opened.stream.size - off)
        return 0;
    if (!wad_seek(&opened.stream, off) || !wad_read(&opened.stream, dst, len))
        return 0;
    return len;
}

wad_file_class_t warden_doom_wad_class = {
    bridge_open, bridge_close, bridge_read
};
