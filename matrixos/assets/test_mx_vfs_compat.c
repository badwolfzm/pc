/*
 * test_mx_vfs_compat — compiles the shim against a stand-in for the baseline's
 * mx_oracle (same field names the shim uses) and drives it the way the
 * security review drove the baseline: a symlink inside the sandbox pointing
 * to ../outside.txt must NOT be readable or writable.
 * Usage: test_mx_vfs_compat <scratch-dir>
 */
#define _GNU_SOURCE
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

/* Minimal stand-in for the baseline definitions the shim depends on. */
enum { MX_CAP_FS_READ = 1u << 0, MX_CAP_FS_WRITE = 1u << 1 };
typedef struct { uint64_t h; bool strict_deterministic; } mx_trace;
typedef struct { int host; uint32_t caps; mx_trace trace; } mx_oracle;
static void mx_trace_event(mx_trace *t, uint32_t tag, uint64_t a, uint64_t b) { t->h ^= tag ^ a ^ (b << 1); }

#include "mx_vfs_compat.h"

static int fails = 0, passes = 0;
#define CHECK(c, m) do { if (c) { passes++; printf("  ok   %s\n", m); } else { fails++; printf("  FAIL %s (line %d)\n", m, __LINE__); } } while (0)
static int run(const char *cmd) { int rc = system(cmd); return WIFEXITED(rc) ? WEXITSTATUS(rc) : -1; }

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <scratch-dir>\n", argv[0]); return 2; }
    const char *dir = argv[1];
    char cmd[8192], root[4096];
    snprintf(root, sizeof root, "%s/sandbox", dir);
    snprintf(cmd, sizeof cmd,
        "rm -rf '%s' && mkdir -p '%s/sandbox/sub' && echo SECRET > '%s/outside.txt' && echo hello > '%s/sandbox/hello.txt' && "
        "ln -s ../outside.txt '%s/sandbox/link.txt' && ln -s .. '%s/sandbox/up' && ln -s /etc/passwd '%s/sandbox/abs'",
        dir, dir, dir, dir, dir, dir, dir);
    if (run(cmd) != 0) { perror("setup"); return 2; }

    mx_oracle o; memset(&o, 0, sizeof o); o.caps = MX_CAP_FS_READ | MX_CAP_FS_WRITE;
    mx_vfs v;
    CHECK(mx_vfs_init(&v, &o, root, true) == 0, "init opens the sandbox directory fd");

    uint8_t *buf = NULL; size_t len = 0;
    CHECK(mx_vfs_read_all(&v, "/sandbox/hello.txt", &buf, &len) == 0 && len == 6 && memcmp(buf, "hello\n", 6) == 0, "plain file beneath root reads");
    free(buf); buf = NULL;
    CHECK(mx_vfs_read_all(&v, "/sandbox/link.txt", &buf, &len) == -103 && buf == NULL, "F1: symlink to ../outside.txt is refused (baseline returned the secret)");
    CHECK(mx_vfs_read_all(&v, "/sandbox/up/outside.txt", &buf, &len) == -103, "path through a symlinked directory is refused");
    CHECK(mx_vfs_read_all(&v, "/sandbox/abs", &buf, &len) == -103, "absolute symlink is refused");
    CHECK(mx_vfs_read_all(&v, "/sandbox/../outside.txt", &buf, &len) == -102, "../ segment refused by the string check");
    CHECK(mx_vfs_read_all(&v, "/sandbox/sub/../../outside.txt", &buf, &len) == -102, "nested ../ refused");
    CHECK(mx_vfs_read_all(&v, "/sandbox/sub", &buf, &len) == -103, "a directory is not a file");
    CHECK(mx_vfs_read_all(&v, "/etc/passwd", &buf, &len) == -102, "outside prefix refused");
    CHECK(mx_vfs_write_all(&v, "/sandbox/link.txt", "pwned", 5) == -103, "F1 write: symlink target is not written through");
    snprintf(cmd, sizeof cmd, "grep -q SECRET '%s/outside.txt'", dir);
    CHECK(run(cmd) == 0, "outside.txt untouched");
    CHECK(mx_vfs_write_all(&v, "/sandbox/sub/new.txt", "data", 4) == 0, "write beneath root works");
    CHECK(mx_vfs_read_all(&v, "/sandbox/sub/new.txt", &buf, &len) == 0 && len == 4, "and reads back");
    free(buf);

    /* F1b: over-long path refused, never truncated. */
    static char longp[2048]; memset(longp, 'a', sizeof longp - 1); memcpy(longp, "/sandbox/", 9); longp[sizeof longp - 1] = 0;
    char host[MX_MAX_PATH];
    CHECK(mx_vfs_read_all(&v, longp, &buf, &len) == -102, "F1b: 2047-byte guest path refused");
    CHECK(mx_vfs_map_guest_to_host(&v, longp, host) == -102, "F1b: map refuses it too");
    char p511[MX_MAX_PATH]; memset(p511, 'b', sizeof p511 - 1); memcpy(p511, "/sandbox/", 9); p511[MX_MAX_PATH - 1] = 0;
    { int rc = mx_vfs_read_all(&v, p511, &buf, &len); CHECK(rc == -102 || rc == -103, "511-byte guest path (fits the baseline buffer exactly; its 502-byte name exceeds NAME_MAX) refused, not truncated"); }
    CHECK(mx_vfs_map_guest_to_host(&v, "/sandbox/hello.txt", host) == 0 && strstr(host, "/sandbox/hello.txt") != NULL, "map still produces the host path for logging");

    /* The baseline's one-byte over-read: a path ending right after a '.' must not read past the terminator (ASan build checks this). */
    char *heap = strdup("/sandbox/a."); CHECK(mx_guest_path_allowed(heap) == true, "trailing '.' in a name is fine and bounds-safe"); free(heap);
    heap = strdup("/sandbox/."); CHECK(mx_guest_path_allowed(heap) == false, "lone '.' segment refused"); free(heap);

    o.caps = 0;
    CHECK(mx_vfs_read_all(&v, "/sandbox/hello.txt", &buf, &len) == -101, "capability check kept");
    mx_vfs_destroy(&v);
    printf("\n%d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
