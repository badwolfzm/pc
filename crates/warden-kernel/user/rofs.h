#ifndef WARDEN_ROFS_H
#define WARDEN_ROFS_H

/* Read-only, capability-scoped virtual filesystem lookup.
 * No host filesystem calls; immutable filenames and file bytes are supplied
 * by the trusted personality server. Grants MUST originate from server policy,
 * never from a guest-provided path or syscall argument.
 */
#include <stddef.h>
#include <stdint.h>

#define ROFS_MAX_PATH 255
#define ROFS_MAX_ENTRIES 1024

enum {
    ROFS_OK = 0,
    ROFS_ENOENT = -2,
    ROFS_EACCES = -13,
    ROFS_EINVAL = -22,
    ROFS_EOVERFLOW = -75
};

struct rofs_entry {
    const char *path; /* trusted, NUL-terminated canonical absolute path */
    const uint8_t *bytes; /* immutable file data, may be NULL iff size == 0 */
    size_t size;
};
struct rofs_grant {
    const char *root; /* trusted canonical absolute root */
    size_t root_len;  /* strlen(root), bounded by ROFS_MAX_PATH */
};

/* Both functions are allocation-free and never modify stored files.
 * Filesystem paths use byte-length input; embedded NUL, traversal, redundant
 * separators, control bytes, and malformed paths are refused.
 */
int rofs_path_valid(const uint8_t *path, size_t len);
int rofs_lookup(const struct rofs_entry *entries, size_t count,
                const struct rofs_grant *grant,
                const uint8_t *path, size_t len,
                const struct rofs_entry **result);
int rofs_read(const struct rofs_entry *entry, size_t offset,
              void *out, size_t capacity, size_t *copied);
#endif
