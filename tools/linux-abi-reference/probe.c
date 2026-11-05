/* Native Linux reference behavior, not a Warden execution test. */
#define _GNU_SOURCE
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#if !defined(__linux__) || !defined(__x86_64__)
#error "This reference probe requires Linux x86-64"
#endif

enum { MASK_SIZE = 65536 };
static unsigned char mask[MASK_SIZE];
static unsigned failed;
static unsigned rows;

/* Only internal constant names are emitted, so no JSON escaping is needed. */
static void record(const char *name, long result, int observed_errno,
                   int expected_errno, int positive, int unchanged) {
    int passed = expected_errno
        ? result == -1 && observed_errno == expected_errno
        : (positive ? result > 0 : result == 0);
    if (expected_errno && unchanged == 0) passed = 0;
    failed += !passed;
    printf("%s{\"name\":\"%s\",\"result\":%ld,\"errno\":%d,"
           "\"passed\":%s,\"output_unchanged\":",
           rows++ ? ",\n" : "", name, result, observed_errno,
           passed ? "true" : "false");
    if (unchanged < 0) printf("null}");
    else printf("%s}", unchanged ? "true" : "false");
}

static int sentinel_intact(void) {
    for (size_t i = 0; i < sizeof mask; i++)
        if (mask[i] != 0xa5) return 0;
    return 1;
}

static void affinity(const char *name, long pid, size_t size,
                     void *out, int expected_errno, int check_sentinel) {
    memset(mask, 0xa5, sizeof mask);
    errno = 0;
    long result = syscall(SYS_sched_getaffinity, pid, size, out);
    int saved_errno = errno;
    int unchanged = check_sentinel ? sentinel_intact() : -1;
    record(name, result, saved_errno, expected_errno, 1, unchanged);
}

static void limit(const char *name, unsigned resource, void *out, int expected_errno) {
    errno = 0;
    long result = syscall(SYS_getrlimit, resource, out);
    record(name, result, errno, expected_errno, 0, -1);
}

static void probe_prlimit(const char *name, long pid, void *out, int expected_errno) {
    errno = 0;
    /* new_limit=NULL: never mutate the host process's resource limits. */
    long result = syscall(SYS_prlimit64, pid, RLIMIT_NOFILE, NULL, out);
    record(name, result, errno, expected_errno, 0, -1);
}

static void resolution(const char *name, long clock_id, void *out, int expected_errno) {
    errno = 0;
    long result = syscall(SYS_clock_getres, clock_id, out);
    record(name, result, errno, expected_errno, 0, -1);
}

int main(void) {
    long page_long = sysconf(_SC_PAGESIZE);
    if (page_long <= 0 || (unsigned long)page_long > SIZE_MAX / 2) {
        fprintf(stderr, "unsupported page size\n");
        return 2;
    }
    size_t page = (size_t)page_long;
    unsigned char *mem = mmap(NULL, 2 * page, PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) { perror("mmap"); return 2; }
    /* Keep a mapped, inaccessible page: no guessed potentially valid address. */
    if (mprotect(mem + page, page, PROT_NONE)) {
        perror("mprotect"); munmap(mem, 2 * page); return 2;
    }
    const int missing_pid_errno = ESRCH;
    printf("{\"schema\":1,\"execution\":\"native-linux-reference\",\n\"cases\":[\n");
    affinity("affinity_self", 0, sizeof mask, mask, 0, 0);
    /* Linux PID_MAX_LIMIT is below INT_MAX; no process is created or signalled. */
    affinity("affinity_missing_pid", INT_MAX, sizeof mask, mask, missing_pid_errno, 1);
    affinity("affinity_short", 0, 0, mask, EINVAL, 1);
    affinity("affinity_unaligned", 0, sizeof mask - 1, mask, EINVAL, 1);
    affinity("affinity_unmapped", 0, sizeof mask, mem + page, EFAULT, 0);

    struct rlimit lim;
    limit("rlimit_self", RLIMIT_NOFILE, &lim, 0);
    limit("rlimit_invalid_resource", UINT_MAX, &lim, EINVAL);
    limit("rlimit_unmapped", RLIMIT_NOFILE, mem + page, EFAULT);
    /* Linux may partially copy on EFAULT: don't require rollback here. */
    limit("rlimit_cross_page", RLIMIT_NOFILE, mem + page - 8, EFAULT);
    probe_prlimit("prlimit_self", 0, &lim, 0);
    probe_prlimit("prlimit_missing_pid", INT_MAX, &lim, missing_pid_errno);
    resolution("clock_invalid", -1, NULL, EINVAL);
    resolution("clock_nullable", CLOCK_MONOTONIC, NULL, 0);

    if (mprotect(mem, page, PROT_READ)) {
        perror("mprotect read-only"); munmap(mem, 2 * page); return 2;
    }
    affinity("affinity_readonly", 0, sizeof mask, mem, EFAULT, 0);
    limit("rlimit_readonly", RLIMIT_NOFILE, mem, EFAULT);
    resolution("clock_readonly", CLOCK_MONOTONIC, mem, EFAULT);
    if (munmap(mem, 2 * page)) { perror("munmap"); return 2; }
    printf("\n],\"failed\":%u}\n", failed);
    return failed ? 1 : 0;
}
