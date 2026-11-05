/*
 * mx_vfs_compat.h — drop-in replacement for the baseline MatrixOS mx_vfs
 * (mx_vfs_init / mx_vfs_map_guest_to_host / mx_vfs_read_all / mx_vfs_write_all)
 * that opens with openat2(RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS |
 * RESOLVE_NO_MAGICLINKS | RESOLVE_NO_XDEV) from the sandbox directory fd
 * instead of validating a string and calling fopen. Fixes security review
 * findings F1 (symlink escape), F1b (silent path truncation) and the one-byte
 * over-read in mx_path_has_dotdot_segment.
 *
 * How to swap it in (header-only, the way the lifter's compat header works):
 *
 *   1. In the integrated build, include this header AFTER the baseline has
 *      defined mx_oracle, mx_trace_event, MX_CAP_FS_READ/WRITE and MX_MAX_PATH,
 *      and BEFORE the baseline's own mx_vfs block.
 *   2. Wrap the baseline block (typedef mx_vfs ... mx_vfs_write_all) in
 *      #ifndef MX_VFS_COMPAT ... #endif. This header defines MX_VFS_COMPAT.
 *
 * Same names, same signatures, same return codes where the baseline had one:
 *   -1 bad args, -101 capability missing, -102 guest path not allowed,
 *   -103 open failed (now also: symlink, mount crossing, not a regular file),
 *   -104 seek/stat, -105 out of memory, -106 short read/write,
 *   -107 path too long (new; the baseline truncated instead).
 * Linux only (openat2, kernel 5.6+). _GNU_SOURCE must be defined before the
 * first system include in the translation unit (O_PATH, openat2 constants);
 * the baseline includes <stdio.h> first without it, so build with -D_GNU_SOURCE.
 *
 * Trace fidelity: the baseline's read_all/write_all went through
 * map_guest_to_host and so emitted a PATH event (tag 0x50415448, strlen(guest),
 * strlen(host path)) before READ/WRITE. This shim emits the same event with the
 * same second argument (the host path length as the baseline's snprintf would
 * have produced it, capped at MX_MAX_PATH-1), so the deterministic trace hash
 * of the integrated build is unchanged.
 */
#ifndef MX_VFS_COMPAT_H
#define MX_VFS_COMPAT_H
#define MX_VFS_COMPAT 1

#include <errno.h>
#include <fcntl.h>
#include <linux/openat2.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#ifndef MX_MAX_PATH
#define MX_MAX_PATH 512
#endif

typedef struct {
    char sandbox_root[MX_MAX_PATH];
    bool deterministic;
    mx_oracle *oracle;
    int dirfd;                 /* O_PATH fd of sandbox_root; every open is relative to it */
} mx_vfs;

/* "/sandbox/" prefix, no empty, "." or ".." segment, no NUL, bounded length.
 * Bounds-safe rewrite of the baseline check: reads p[i+1]/p[i+2] only after
 * confirming the previous byte is not the terminator. */
static inline bool mx_guest_path_allowed(const char *guest) {
    if (!guest) return false;
    size_t n = strnlen(guest, MX_MAX_PATH);
    if (n >= MX_MAX_PATH) return false;                       /* too long: refused, not truncated */
    if (strncmp(guest, "/sandbox/", 9) != 0) return false;
    const char *p = guest + 9;
    if (*p == 0) return false;                                /* the root itself is not a file */
    while (*p) {
        const char *seg = p;
        while (*p && *p != '/' && *p != '\\') ++p;
        size_t sl = (size_t)(p - seg);
        if (sl == 0) return false;                            /* "//" or trailing "/" */
        if (sl == 1 && seg[0] == '.') return false;
        if (sl == 2 && seg[0] == '.' && seg[1] == '.') return false;
        if (*p) ++p;
    }
    return true;
}

static inline int mx_vfs_init(mx_vfs *v, mx_oracle *o, const char *sandbox_root, bool deterministic) {
    if (!v || !o || !sandbox_root) return -1;
    memset(v, 0, sizeof(*v));
    if (strnlen(sandbox_root, MX_MAX_PATH) >= MX_MAX_PATH) return -107;
    memcpy(v->sandbox_root, sandbox_root, strlen(sandbox_root) + 1);
    v->deterministic = deterministic;
    v->oracle = o;
    v->dirfd = open(sandbox_root, O_PATH | O_DIRECTORY | O_CLOEXEC);
    if (v->dirfd < 0) return -103;
    mx_trace_event(&o->trace, 0x56465349ULL, (uint64_t)strlen(sandbox_root), deterministic);
    return 0;
}

static inline void mx_vfs_destroy(mx_vfs *v) {
    if (v && v->dirfd >= 0) { close(v->dirfd); v->dirfd = -1; }
}

/* Kept for callers that log the host path. The result is informational only:
 * no open in this header uses it. */
static inline int mx_vfs_map_guest_to_host(mx_vfs *v, const char *guest, char out_host[MX_MAX_PATH]) {
    if (!v || !guest || !out_host) return -1;
    if (!(v->oracle->caps & MX_CAP_FS_READ)) return -101;
    if (!mx_guest_path_allowed(guest)) return -102;
    int n = snprintf(out_host, MX_MAX_PATH, "%s/%s", v->sandbox_root, guest + 9);
    if (n < 0 || n >= MX_MAX_PATH) { out_host[0] = 0; return -107; }
    mx_trace_event(&v->oracle->trace, 0x50415448ULL, (uint64_t)strlen(guest), (uint64_t)n);
    return 0;
}

/* Emit the PATH trace event exactly as the baseline's map_guest_to_host did. */
static inline void mx_vfs_trace_path_(mx_vfs *v, const char *guest) {
    size_t n = strlen(v->sandbox_root) + 1 + strlen(guest + 9);
    if (n > MX_MAX_PATH - 1) n = MX_MAX_PATH - 1;             /* the baseline's buffer would have truncated here */
    mx_trace_event(&v->oracle->trace, 0x50415448ULL, (uint64_t)strlen(guest), (uint64_t)n);
}

static inline int mx_vfs_open_beneath_(mx_vfs *v, const char *guest, uint64_t flags, uint64_t mode) {
    if (!mx_guest_path_allowed(guest)) return -102;
    mx_vfs_trace_path_(v, guest);
    struct open_how how;
    memset(&how, 0, sizeof how);
    how.flags   = flags | O_CLOEXEC | O_NOCTTY;
    how.mode    = mode;
    how.resolve = RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS | RESOLVE_NO_MAGICLINKS | RESOLVE_NO_XDEV;
    long fd = syscall(SYS_openat2, v->dirfd, guest + 9, &how, sizeof how);
    if (fd < 0) return -103;
    struct stat st;
    if (fstat((int)fd, &st) != 0) { close((int)fd); return -104; }
    if (!S_ISREG(st.st_mode)) { close((int)fd); return -103; }
    return (int)fd;
}

static inline int mx_vfs_read_all(mx_vfs *v, const char *guest, uint8_t **out_buf, size_t *out_len) {
    if (!v || !guest || !out_buf || !out_len) return -1;
    if (!(v->oracle->caps & MX_CAP_FS_READ)) return -101;
    int fd = mx_vfs_open_beneath_(v, guest, O_RDONLY, 0);
    if (fd < 0) return fd;
    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size < 0) { close(fd); return -104; }
    size_t sz = (size_t)st.st_size;
    uint8_t *buf = (uint8_t *)malloc(sz + 1);
    if (!buf) { close(fd); return -105; }
    size_t got = 0;
    while (got < sz) {
        ssize_t r = pread(fd, buf + got, sz - got, (off_t)got);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) { close(fd); free(buf); return -106; }
        got += (size_t)r;
    }
    close(fd);
    buf[sz] = 0;
    *out_buf = buf;
    *out_len = sz;
    mx_trace_event(&v->oracle->trace, 0x52454144ULL, (uint64_t)sz, 0);
    return 0;
}

static inline int mx_vfs_write_all(mx_vfs *v, const char *guest, const void *buf, size_t len) {
    if (!v || !guest || !buf) return -1;
    if (!(v->oracle->caps & MX_CAP_FS_WRITE)) return -101;
    if (!(v->oracle->caps & MX_CAP_FS_READ)) return -101;
    /* O_CREAT with RESOLVE_NO_SYMLINKS: an existing symlink at the target is
     * refused rather than followed; the parent directory must be beneath root. */
    int fd = mx_vfs_open_beneath_(v, guest, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return fd;
    size_t off = 0;
    const uint8_t *p = (const uint8_t *)buf;
    while (off < len) {
        ssize_t w = write(fd, p + off, len - off);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) { close(fd); return -106; }
        off += (size_t)w;
    }
    close(fd);
    mx_trace_event(&v->oracle->trace, 0x57524954ULL, (uint64_t)len, 0);
    return 0;
}

#endif /* MX_VFS_COMPAT_H */
