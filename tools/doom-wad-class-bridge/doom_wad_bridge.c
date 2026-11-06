#include "doom_wad_bridge.h"
#include <string.h>

/* At most one live open, but each successful open has a distinct address.
 * Non-reusable slots prevent stale wad_file_t* values from acquiring a
 * later grant's authority. Slot exhaustion is an explicit, safe failure.
 */
#define BRIDGE_PATH_MAX 128u
#define BRIDGE_MAX_LIFETIME_OPENS 64u
static struct {
    char path[BRIDGE_PATH_MAX];
    void *ctx;
    wad_read_at reader;
    uint32_t bytes;
    unsigned int granted;
} grant;
typedef struct {
    wad_file_t file;
    wad_stream stream;
    unsigned int active;
} bridge_handle;
static bridge_handle handles[BRIDGE_MAX_LIFETIME_OPENS];
static bridge_handle *current;
static unsigned int used_handles;

void warden_doom_wad_revoke(void) {
    /* Fail closed on revoke, retaining all previously used handle addresses. */
    grant.granted = 0;
    grant.ctx = NULL;
    grant.reader = NULL;
    grant.bytes = 0;
    memset(grant.path, 0, sizeof grant.path);
    if (current) {
        current->active = 0;
        wad_close(&current->stream);
        current = NULL;
    }
}

int warden_doom_wad_grant(const char *path, void *ctx, wad_read_at reader,
                         uint32_t source_bytes) {
    warden_doom_wad_revoke();
    if (!path || !reader || source_bytes < 12 || source_bytes > WAD_MAX_SIZE)
        return 0;
    size_t n = 0;
    while (n < BRIDGE_PATH_MAX && path[n] != '\0') ++n;
    if (n == 0 || n == BRIDGE_PATH_MAX) return 0;
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
    if (!path || !grant.granted || current ||
        used_handles == BRIDGE_MAX_LIFETIME_OPENS ||
        strcmp(path, grant.path) != 0) return NULL;
    bridge_handle *next = &handles[used_handles];
    if (!wad_open(&next->stream, grant.ctx, grant.reader, grant.bytes))
        return NULL;
    next->file.file_class = &warden_doom_wad_class;
    next->file.mapped = NULL;
    next->file.length = grant.bytes;
    next->active = 1;
    current = next;
    ++used_handles; /* Never rewind, not even across revoke/regrant. */
    return &next->file;
}

static void bridge_close(wad_file_t *f) {
    if (!current || f != &current->file || !current->active) return;
    current->active = 0;
    wad_close(&current->stream);
    current = NULL;
}

static size_t bridge_read(wad_file_t *f, unsigned int off, void *dst,
                          size_t len) {
    if (!current || f != &current->file || !current->active ||
        !grant.granted || (!dst && len)) return 0;
    if (len == 0 || off > current->stream.size ||
        len > current->stream.size - off) return 0;
    if (!wad_seek(&current->stream, off) ||
        !wad_read(&current->stream, dst, len)) return 0;
    return len;
}

wad_file_class_t warden_doom_wad_class = {
    bridge_open, bridge_close, bridge_read
};
