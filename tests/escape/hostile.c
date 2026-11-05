/* Hostile workload for the Phase 0 escape suite.
 * Runs INSIDE the sandbox and tries to get out. Every check prints
 * "PASS <name>" when the sandbox held and "FAIL <name>" when it did not.
 * Usage: hostile WDIR ALLOWED_PORT FORBIDDEN_PORT
 *   WDIR is the only writable directory the manifest grants.
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
static volatile int racing = 1;
static void *flip_path(void *a) {
    (void)a;
    while (racing) {
        strcpy(race_path, "/etc/passwd");
        strcpy(race_path, ok_path);
    }
    return NULL;
}

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
    snprintf(ok_path, sizeof ok_path, "%s/ok", w);
    { int fd = open(ok_path, O_CREAT | O_WRONLY | O_TRUNC, 0600); if (write(fd, "harmless\n", 9) != 9) perror("write"); close(fd); }
    strcpy(race_path, ok_path);
    pthread_t t; racing = 1;
    pthread_create(&t, NULL, flip_path, NULL);
    int leaked = 0, opened = 0;
    for (int i = 0; i < 3000; i++) {
        int fd = open(race_path, O_RDONLY);
        if (fd >= 0) opened++;
        if (reads_secret(fd)) leaked++;
    }
    racing = 0; pthread_join(t, NULL);
    snprintf(p, sizeof p, "3000 tries, %d opened, %d leaked", opened, leaked);
    check("open() path race", leaked == 0, p);

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
