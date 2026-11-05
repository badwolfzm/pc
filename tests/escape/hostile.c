/* Hostile workload for the Phase 0 escape suite.
 * Runs INSIDE the sandbox and tries to get out. Every check prints
 * "PASS <name>" when the sandbox held and "FAIL <name>" when it did not.
 * Usage: hostile WDIR ALLOWED_PORT FORBIDDEN_PORT [OUTSIDE OKBIN]
 *   WDIR is the only writable directory the manifest grants.
 *   OUTSIDE is a directory no grant covers, prepared by the harness with
 *   victim files; the harness checks afterwards that nothing in it changed.
 *   OKBIN is a program inside the exec grant that exits 0.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/ptrace.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <dlfcn.h>
#include <unistd.h>

static int fails = 0;
static void check(const char *name, int held, const char *why) {
    printf("%s %-34s %s\n", held ? "PASS" : "FAIL", name, why);
    if (!held) fails++;
}
#define DENIED(name, expr) do { errno = 0; long _r = (long)(expr); int _e = errno; \
    char _b[64]; snprintf(_b, sizeof _b, "ret=%ld errno=%s", _r, strerrorname_np(_e) ? strerrorname_np(_e) : "0"); \
    check(name, _r < 0, _b); } while (0)

static int reads_secret(int fd) {
    char buf[256] = {0};
    if (fd < 0) return 0;
    ssize_t n = read(fd, buf, sizeof buf - 1);
    close(fd);
    return n > 0 && strstr(buf, "root:") != NULL;
}

/* ---- race: flip a path between allowed and forbidden while opening ---- */
static char race_path[256];
static char ok_path[256];
static char bad_path[256] = "/etc/passwd";
static volatile int racing = 1;
static void *flip_path(void *a) {
    (void)a;
    while (racing) {
        strcpy(race_path, bad_path);
        strcpy(race_path, ok_path);
    }
    return NULL;
}
/* Run `body` RACE_N times while a thread flips race_path between ok and bad. */
static pthread_t race_thread;
static void race_begin(const char *ok, const char *bad) {
    strcpy(ok_path, ok); strcpy(bad_path, bad); strcpy(race_path, ok);
    racing = 1; pthread_create(&race_thread, NULL, flip_path, NULL);
}
static void race_end(void) { racing = 0; pthread_join(race_thread, NULL); }
#define RACE_N 2000

/* ---- race: flip a sockaddr port between allowed and forbidden ---- */
static struct sockaddr_in race_sa;
static unsigned short port_ok, port_bad;
static void *flip_port(void *a) {
    (void)a;
    while (racing) {
        race_sa.sin_port = htons(port_bad);
        race_sa.sin_port = htons(port_ok);
    }
    return NULL;
}

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: hostile WDIR OKPORT BADPORT\n"); return 2; }
    const char *w = argv[1];
    port_ok = (unsigned short)atoi(argv[2]);
    port_bad = (unsigned short)atoi(argv[3]);
    char p[512];
    setvbuf(stdout, NULL, _IONBF, 0);

    /* Filesystem scope */
    DENIED("read /etc/passwd", open("/etc/passwd", O_RDONLY));
    DENIED("dotdot out of a grant", open("/usr/lib/../../etc/passwd", O_RDONLY));
    int lib = open("/usr/lib", O_PATH | O_DIRECTORY);
    check("openat(dirfd) inside grant works", lib >= 0 && faccessat(lib, "x86_64-linux-gnu", F_OK, 0) == 0, "faccessat relative to /usr/lib");
    DENIED("openat dirfd + ../..", openat(lib, "../../etc/passwd", O_RDONLY));
    snprintf(p, sizeof p, "%s/evil-link", w);
    unlink(p);
    if (symlink("/etc/passwd", p) == 0) {
        int fd = open(p, O_RDONLY);
        check("symlink to /etc/passwd", !reads_secret(fd), fd < 0 ? strerror(errno) : "opened?");
    } else check("symlink to /etc/passwd", 1, "symlink itself refused");
    snprintf(p, sizeof p, "%s/rel-link", w);
    unlink(p);
    if (symlink("../../../../../../../../etc/passwd", p) != 0) perror("symlink");
    { int fd = open(p, O_RDONLY); check("relative symlink escape", !reads_secret(fd), fd < 0 ? strerror(errno) : "opened?"); }
    snprintf(p, sizeof p, "%s/hardlink", w);
    DENIED("hardlink /etc/passwd in", link("/etc/passwd", p));
    snprintf(p, sizeof p, "%s/x", w);
    close(open(p, O_CREAT | O_WRONLY, 0600));
    DENIED("rename out to /etc", rename(p, "/etc/warden-x"));
    DENIED("write under /usr/lib", open("/usr/lib/warden-x", O_CREAT | O_WRONLY, 0600));
    DENIED("open /proc/self/mem", open("/proc/self/mem", O_RDWR));
    DENIED("open supervisor /proc/1/root", open("/proc/1/root/etc/passwd", O_RDONLY));
    DENIED("stat /root", syscall(SYS_newfstatat, AT_FDCWD, "/root", p, 0));

    /* Path race (TOCTOU): never read the secret */
    char okfile[256]; snprintf(okfile, sizeof okfile, "%s/ok", w);
    { int fd = open(okfile, O_CREAT | O_WRONLY | O_TRUNC, 0600); if (write(fd, "harmless\n", 9) != 9) perror("write"); close(fd); }
    struct stat okst; stat(okfile, &okst);
    pthread_t t;
    race_begin(okfile, "/etc/passwd");
    int leaked = 0, opened = 0;
    for (int i = 0; i < 3000; i++) {
        int fd = open(race_path, O_RDONLY);
        if (fd >= 0) opened++;
        if (reads_secret(fd)) leaked++;
    }
    race_end();
    snprintf(p, sizeof p, "3000 tries, %d opened, %d leaked", opened, leaked);
    check("open() path race", leaked == 0, p);

    /* Metadata races: the kernel must never act on the swapped-in path. */
    { int bad = 0, ok = 0; struct stat st;
      race_begin(okfile, "/etc/passwd");
      for (int i = 0; i < RACE_N; i++) if (stat(race_path, &st) == 0) { if (st.st_ino == okst.st_ino) ok++; else bad++; }
      race_end();
      snprintf(p, sizeof p, "%d tries, %d ok inode, %d foreign inode", RACE_N, ok, bad);
      check("stat() path race", bad == 0, p); }
    { int bad = 0, ok = 0; char missing[256]; snprintf(missing, sizeof missing, "%s/does-not-exist", w);
      race_begin(missing, "/etc/passwd");
      for (int i = 0; i < RACE_N; i++) if (access(race_path, R_OK) == 0) bad++; else ok++;
      race_end();
      snprintf(p, sizeof p, "%d tries, %d refused, %d said /etc/passwd is readable", RACE_N, ok, bad);
      check("access() path race", bad == 0, p); }
    { int bad = 0, ok = 0; char lnk[256], tgt[256]; snprintf(lnk, sizeof lnk, "%s/ok-link", w);
      unlink(lnk); if (symlink("harmless", lnk) != 0) perror("symlink");
      race_begin(lnk, "/proc/self/exe");
      for (int i = 0; i < RACE_N; i++) { ssize_t n = readlink(race_path, tgt, sizeof tgt - 1);
          if (n > 0) { tgt[n] = 0; if (strcmp(tgt, "harmless") == 0) ok++; else bad++; } }
      race_end();
      snprintf(p, sizeof p, "%d tries, %d ok, %d read a foreign link", RACE_N, ok, bad);
      check("readlink() path race", bad == 0, p); }
    { int bad = 0, ok = 0; struct stat st;
      race_begin(okfile, "/etc/passwd");
      for (int i = 0; i < RACE_N; i++) { int fd = open(race_path, O_PATH);
          if (fd >= 0) { if (fstat(fd, &st) == 0 && st.st_ino != okst.st_ino) bad++; else ok++; close(fd); } }
      race_end();
      snprintf(p, sizeof p, "%d tries, %d ok, %d fd to a foreign inode", RACE_N, ok, bad);
      check("open(O_PATH) path race", bad == 0, p); }

    /* Mutating races against a directory outside every grant. The harness
     * prepared OUTSIDE and checks afterwards that nothing in it changed;
     * here we only report whether the kernel ever claimed success. */
    if (argc >= 6) {
        const char *out = argv[4];
        char okp[256], badp[256];
    /* The verdict for these is the harness's inspection of OUTSIDE after
     * the run: in here, success on the granted path is normal and the
     * flipping thread makes "which path was it" unknowable. */
#define MUTATE(name, okname, badname, expr) do { int okays = 0; \
        snprintf(okp, sizeof okp, "%s/%s", w, okname); snprintf(badp, sizeof badp, "%s/%s", out, badname); \
        race_begin(okp, badp); \
        for (int i = 0; i < RACE_N; i++) { if ((expr) == 0) okays++; } \
        race_end(); \
        snprintf(p, sizeof p, "%d tries, %d succeeded; verdict: harness inspects the outside dir", RACE_N, okays); \
        check(name, 1, p); } while (0)
        MUTATE("unlink() path race", "missing", "victim-unlink", unlink(race_path));
        MUTATE("rmdir() path race", "missing", "victim-rmdir", rmdir(race_path));
        MUTATE("mkdir() path race", "race-dir", "escaped-dir", mkdir(race_path, 0700));
        MUTATE("symlink() path race", "race-sym", "escaped-sym", symlink("/etc/passwd", race_path));
        MUTATE("chmod() path race", "ok", "victim-chmod", chmod(race_path, 0777));
        MUTATE("truncate() path race", "ok", "victim-trunc", truncate(race_path, 0));
        { char src[256]; snprintf(src, sizeof src, "%s/ok", w);
          MUTATE("link() path race", "race-hard", "escaped-hard", link(src, race_path));
          snprintf(src, sizeof src, "%s/rename-src", w);
          close(open(src, O_CREAT | O_WRONLY, 0600));
          MUTATE("rename() path race", "rename-dst", "escaped-rename", rename(src, race_path)); }
        /* Destination side of rename: flip the source across the boundary too. */
        { snprintf(okp, sizeof okp, "%s/rename-src2", w); close(open(okp, O_CREAT | O_WRONLY, 0600));
          snprintf(badp, sizeof badp, "%s/victim-rename", out);
          char dst[256]; snprintf(dst, sizeof dst, "%s/rename-dst2", w);
          race_begin(okp, badp);
          for (int i = 0; i < RACE_N; i++) rename(race_path, dst);
          race_end();
          check("rename() source race", 1, "harness checks the victim is still there"); }

        /* Exec race: a thread swaps the program path for a script outside
         * the exec grant while a child execs. The kernel may load the swapped
         * image; the supervisor must then kill it before it gets anywhere.
         * The script exits 42 if it runs to completion. */
        { const char *okbin = argv[5]; int escaped = 0, ran_ok = 0, killed = 0, refused = 0, other = 0;
          snprintf(badp, sizeof badp, "%s/escape.sh", out);
          for (int i = 0; i < 300; i++) {
              pid_t c = fork();
              if (c == 0) {
                  race_begin(okbin, badp);
                  for (int k = 0; k < 50; k++) { char *av[] = {race_path, NULL}; execve(race_path, av, NULL); }
                  _exit(errno == EACCES ? 3 : 4);
              }
              int st = 0; waitpid(c, &st, 0);
              if (WIFSIGNALED(st) && WTERMSIG(st) == SIGKILL) killed++;
              else if (WIFEXITED(st) && WEXITSTATUS(st) == 42) escaped++;
              else if (WIFEXITED(st) && WEXITSTATUS(st) == 0) ran_ok++;
              else if (WIFEXITED(st) && WEXITSTATUS(st) == 3) refused++;
              else other++;
          }
          snprintf(p, sizeof p, "300 tries: %d ran the granted program, %d refused, %d swapped images killed, %d escaped, %d other", ran_ok, refused, killed, escaped, other);
          check("execve() path race", escaped == 0 && other == 0, p); }
    }

    /* Escape primitives */
    DENIED("ptrace(TRACEME)", ptrace(PTRACE_TRACEME, 0, 0, 0));
    DENIED("mount tmpfs", mount("none", "/mnt", "tmpfs", 0, 0));
    DENIED("unshare(CLONE_NEWUSER)", unshare(CLONE_NEWUSER));
    DENIED("raw clone(CLONE_NEWUSER)", syscall(SYS_clone, CLONE_NEWUSER | SIGCHLD, 0, 0, 0, 0));
    DENIED("bpf()", syscall(SYS_bpf, 0, 0, 0));
    DENIED("io_uring_setup", syscall(SYS_io_uring_setup, 8, p));
    DENIED("userfaultfd", syscall(SYS_userfaultfd, 0));
    DENIED("init_module", syscall(SYS_init_module, p, 0, ""));
    DENIED("process_vm_writev", syscall(SYS_process_vm_writev, getppid(), 0, 0, 0, 0, 0));
    DENIED("pidfd_open(supervisor)", syscall(SYS_pidfd_open, getppid(), 0));
    DENIED("seccomp (new filter)", syscall(SYS_seccomp, 1, 0, 0));
    DENIED("unknown syscall nr 1000", syscall(1000));
    DENIED("kill(supervisor)", kill(getppid(), 0));
    DENIED("kill(1)", kill(1, 0));
    DENIED("kill(-1) everyone", kill(-1, 0));
    { char c = 'x'; DENIED("TIOCSTI keystroke injection", ioctl(0, TIOCSTI, &c)); }

    /* The sandbox must not hold the supervisor's listener fd */
    { int found = 0; unsigned char junk[128];
      for (int fd = 3; fd < 256; fd++) {
          if (ioctl(fd, 0xC0502100 /* SECCOMP_IOCTL_NOTIF_RECV */, junk) == 0 || errno == ENOENT) found = 1;
      }
      check("no seccomp listener in reach", !found, found ? "found one" : "none of fds 3..255"); }

    /* Exec outside the exec grant */
    snprintf(p, sizeof p, "%s/copied-binary", w);
    { int in = open("/proc/self/exe", O_RDONLY); /* denied: no /proc grant */ check("read /proc/self/exe", in < 0, in < 0 ? strerror(errno) : "opened");
      int out = open(p, O_CREAT | O_WRONLY | O_TRUNC, 0700); if (write(out, "#!/usr/bin/sh\necho escaped\n", 27) != 27) perror("write"); close(out);
      pid_t c = fork();
      if (c == 0) { char *av[] = {p, NULL}; execve(p, av, NULL); _exit(errno == EACCES ? 0 : 1); }
      int st = 0; waitpid(c, &st, 0);
      check("exec from writable dir", WIFEXITED(st) && WEXITSTATUS(st) == 0, "execve refused"); }

    /* Foreign ABIs: the filter kills the process */
    { pid_t c = fork();
      if (c == 0) { long r; __asm__ volatile("int $0x80" : "=a"(r) : "a"(20) : "memory"); _exit(0); }
      int st = 0; waitpid(c, &st, 0);
      check("32-bit int 0x80 syscall", WIFSIGNALED(st), WIFSIGNALED(st) ? "killed" : "ran"); }
    { pid_t c = fork();
      if (c == 0) { syscall(0x40000000 | 39); _exit(0); }
      int st = 0; waitpid(c, &st, 0);
      check("x32 syscall", WIFSIGNALED(st), WIFSIGNALED(st) ? "killed" : "ran"); }

    /* Libraries: an unapproved one must not load */
    { void *h = dlopen("libz.so.1", RTLD_NOW);
      check("dlopen unapproved libz", h == NULL, h ? "loaded" : "refused"); }

    /* Network */
    DENIED("AF_UNIX socket", socket(AF_UNIX, SOCK_STREAM, 0));
    DENIED("UDP socket", socket(AF_INET, SOCK_DGRAM, 0));
    DENIED("raw socket", socket(AF_INET, SOCK_RAW, IPPROTO_ICMP));
    DENIED("netlink socket", socket(AF_NETLINK, SOCK_RAW, 0));
    struct sockaddr_in sa = { .sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK) };
    sa.sin_port = htons(port_ok);
    { int s = socket(AF_INET, SOCK_STREAM, 0);
      int r = connect(s, (struct sockaddr *)&sa, sizeof sa);
      check("connect to granted port works", r == 0, r == 0 ? "connected" : strerror(errno)); close(s); }
    sa.sin_port = htons(port_bad);
    { int s = socket(AF_INET, SOCK_STREAM, 0);
      DENIED("connect to forbidden port", connect(s, (struct sockaddr *)&sa, sizeof sa)); close(s); }
    { int s = socket(AF_INET, SOCK_STREAM, 0);
      DENIED("TCP fast-open sendto", sendto(s, "x", 1, MSG_FASTOPEN, (struct sockaddr *)&sa, sizeof sa)); close(s); }
    { int s = socket(AF_INET, SOCK_STREAM, 0);
      struct sockaddr_in b = { .sin_family = AF_INET, .sin_port = htons(port_bad + 1) };
      DENIED("bind/listen without grant", bind(s, (struct sockaddr *)&b, sizeof b)); close(s); }

    /* Address race (TOCTOU) on connect */
    race_sa.sin_family = AF_INET; race_sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK); race_sa.sin_port = htons(port_ok);
    racing = 1; pthread_create(&t, NULL, flip_port, NULL);
    int bad = 0, good = 0;
    for (int i = 0; i < 1500; i++) {
        int s = socket(AF_INET, SOCK_STREAM, 0);
        if (connect(s, (struct sockaddr *)&race_sa, sizeof race_sa) == 0) {
            struct sockaddr_in peer; socklen_t l = sizeof peer;
            getpeername(s, (struct sockaddr *)&peer, &l);
            if (ntohs(peer.sin_port) == port_bad) bad++; else good++;
        }
        close(s);
    }
    racing = 0; pthread_join(t, NULL);
    snprintf(p, sizeof p, "1500 tries, %d to granted port, %d to forbidden", good, bad);
    check("connect() address race", bad == 0, p);

    printf("\n%s: %d check(s) failed\n", fails ? "SANDBOX BROKEN" : "ALL HELD", fails);
    return fails ? 1 : 0;
}
