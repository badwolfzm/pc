/* Freestanding Linux x86-64 syscall layer for the Warden Linux test programs.
 * No libc: these are static, non-PIE ELF executables that use the plain
 * Linux ABI (the `syscall` instruction, Linux numbers, negative errno). */
#ifndef WARDEN_SYS_H
#define WARDEN_SYS_H
typedef unsigned long u64;
typedef long i64;
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;

static inline i64 sys6(i64 n, i64 a, i64 b, i64 c, i64 d, i64 e, i64 f) {
    i64 r;
    register i64 r10 __asm__("r10") = d;
    register i64 r8 __asm__("r8") = e;
    register i64 r9 __asm__("r9") = f;
    __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9) : "rcx", "r11", "memory");
    return r;
}
#define sys0(n) sys6(n, 0, 0, 0, 0, 0, 0)
#define sys1(n, a) sys6(n, (i64)(a), 0, 0, 0, 0, 0)
#define sys2(n, a, b) sys6(n, (i64)(a), (i64)(b), 0, 0, 0, 0)
#define sys3(n, a, b, c) sys6(n, (i64)(a), (i64)(b), (i64)(c), 0, 0, 0)
#define sys4(n, a, b, c, d) sys6(n, (i64)(a), (i64)(b), (i64)(c), (i64)(d), 0, 0)

enum {
    SYS_read = 0, SYS_write = 1, SYS_open = 2, SYS_close = 3, SYS_fstat = 5, SYS_mmap = 9, SYS_mprotect = 10, SYS_writev = 20,
    SYS_munmap = 11, SYS_brk = 12, SYS_socket = 41, SYS_connect = 42, SYS_accept = 43, SYS_sendfile = 40,
    SYS_bind = 49, SYS_listen = 50, SYS_setsockopt = 54, SYS_exit = 60, SYS_uname = 63, SYS_ptrace = 101,
    SYS_readlink = 89, SYS_arch_prctl = 158, SYS_nanosleep = 35, SYS_exit_group = 231, SYS_epoll_wait = 232,
    SYS_epoll_ctl = 233, SYS_openat = 257, SYS_epoll_create1 = 291, SYS_accept4 = 288, SYS_bpf = 321,
};

struct sockaddr_in { u16 family; u16 port; u32 addr; u8 zero[8]; };
struct epoll_event { u32 events; u64 data; } __attribute__((packed));
struct timespec { i64 sec; i64 nsec; };
struct iovec { void *base; u64 len; };

static inline u16 htons(u16 x) { return (u16)((x << 8) | (x >> 8)); }
static inline u64 slen(const char *s) { u64 n = 0; while (s[n]) n++; return n; }
static inline void mcpy(void *d, const void *s, u64 n) { u8 *dd = d; const u8 *ss = s; while (n--) *dd++ = *ss++; }
static inline int starts(const char *s, const char *p) { while (*p) if (*s++ != *p++) return 0; return 1; }
static inline void puts1(const char *s) { sys3(SYS_write, 1, s, slen(s)); }

/* "CHECK <id> <value>\n" on stdout: the kernel's judge reads these. */
static inline void check(int id, i64 value) {
    char b[64]; int i = 0;
    const char *h = "CHECK "; while (*h) b[i++] = *h++;
    char t[24]; int n = 0; u64 v = (u64)id;
    do { t[n++] = '0' + v % 10; v /= 10; } while (v);
    while (n) b[i++] = t[--n];
    b[i++] = ' ';
    if (value < 0) { b[i++] = '-'; v = (u64)(-value); } else v = (u64)value;
    do { t[n++] = '0' + v % 10; v /= 10; } while (v);
    while (n) b[i++] = t[--n];
    b[i++] = '\n';
    sys3(SYS_write, 1, b, i);
}

static inline void sleep_ms(int ms) { struct timespec ts = { ms / 1000, (i64)(ms % 1000) * 1000000 }; sys2(SYS_nanosleep, &ts, 0); }

static inline __attribute__((noreturn)) void exit_group(int c) { for (;;) sys1(SYS_exit_group, c); }

/* gcc may emit these for struct copies even without libc. */
void *memset(void *d, int c, u64 n) { u8 *p = d; while (n--) *p++ = (u8)c; return d; }
void *memcpy(void *d, const void *s, u64 n) { mcpy(d, s, n); return d; }

void prog(void);
__attribute__((naked, noreturn)) void _start(void) {
    __asm__ volatile("xor %ebp, %ebp\n\tand $-16, %rsp\n\tcall prog\n\tmov $231, %eax\n\txor %edi, %edi\n\tsyscall\n\thlt");
}
#endif
