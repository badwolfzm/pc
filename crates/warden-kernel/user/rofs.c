#include "rofs.h"

static size_t bounded_strlen(const char *p, size_t max) {
    if (!p) return max + 1;
    size_t n = 0;
    while (n <= max && p[n] != 0) ++n;
    return n;
}
static int equal_bytes(const uint8_t *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; ++i)
        if (a[i] != (uint8_t)b[i]) return 0;
    return 1;
}
int rofs_path_valid(const uint8_t *path, size_t len) {
    if (!path || len < 1 || len > ROFS_MAX_PATH || path[0] != '/')
        return ROFS_EINVAL;
    if (len == 1) return ROFS_OK;
    if (path[len - 1] == '/') return ROFS_EINVAL;
    size_t start = 1;
    for (size_t i = 1; i <= len; ++i) {
        if (i < len && (path[i] < 32 || path[i] == 127 || path[i] == '\\'))
            return ROFS_EINVAL;
        if (i == len || path[i] == '/') {
            size_t n = i - start;
            if (n == 0 || (n == 1 && path[start] == '.') ||
                (n == 2 && path[start] == '.' && path[start + 1] == '.'))
                return ROFS_EINVAL;
            start = i + 1;
        }
    }
    return ROFS_OK;
}
int rofs_lookup(const struct rofs_entry *entries, size_t count,
                const struct rofs_grant *grant,
                const uint8_t *path, size_t len,
                const struct rofs_entry **result) {
    if (!result) return ROFS_EINVAL;
    *result = 0;
    if (!entries || !grant || !grant->root || count > ROFS_MAX_ENTRIES)
        return ROFS_EINVAL;
    if (rofs_path_valid(path, len) != ROFS_OK) return ROFS_EINVAL;
    size_t root_len = bounded_strlen(grant->root, ROFS_MAX_PATH);
    if (root_len != grant->root_len ||
        rofs_path_valid((const uint8_t *)grant->root, root_len) != ROFS_OK)
        return ROFS_EINVAL;
    /* Exact root or component-boundary prefix only: /site != /siteevil.
     * A grant for "/" covers every canonical absolute path.
     */
    if (root_len > 1) {
        if (len < root_len || !equal_bytes(path, grant->root, root_len) ||
            (len != root_len && path[root_len] != '/'))
            return ROFS_EACCES;
    }
    for (size_t i = 0; i < count; ++i) {
        const struct rofs_entry *e = &entries[i];
        if (!e->path || (e->size && !e->bytes)) return ROFS_EINVAL;
        size_t n = bounded_strlen(e->path, ROFS_MAX_PATH);
        if (n > ROFS_MAX_PATH ||
            rofs_path_valid((const uint8_t *)e->path, n) != ROFS_OK)
            return ROFS_EINVAL;
        if (len == n && equal_bytes(path, e->path, n)) {
            *result = e;
            return ROFS_OK;
        }
    }
    return ROFS_ENOENT;
}
int rofs_read(const struct rofs_entry *entry, size_t offset,
              void *out, size_t capacity, size_t *copied) {
    if (!copied) return ROFS_EINVAL;
    *copied = 0;
    if (!entry || (entry->size && !entry->bytes) ||
        (!out && capacity)) return ROFS_EINVAL;
    if (offset >= entry->size || capacity == 0) return ROFS_OK;
    size_t n = entry->size - offset;
    if (n > capacity) n = capacity;
    uint8_t *dst = (uint8_t *)out;
    for (size_t i = 0; i < n; ++i) dst[i] = entry->bytes[offset + i];
    *copied = n;
    return ROFS_OK;
}
