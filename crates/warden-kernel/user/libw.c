#include "libw.h"

/* Entry point: the kernel starts the thread with rsp = STACK_TOP and every
 * other register zero. */
__asm__(".section .text.start,\"ax\"\n"
        ".globl _start\n"
        "_start:\n"
        "  xor %ebp, %ebp\n"
        "  and $-16, %rsp\n"
        "  call main\n"
        "  mov %eax, %edi\n"
        "  call _exit\n"
        "1: jmp 1b\n"
        ".previous\n");

long wsys(long nr, long a, long b, long c) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(nr), "D"(a), "S"(b), "d"(c) : "memory", "rcx", "r11");
    return r;
}

static long wsend_op(long op, long slot, long w0, long w1, long w2, long w3) {
    long r;
    register long r10 __asm__("r10") = w2;
    register long r8 __asm__("r8") = w3;
    __asm__ volatile("int $0x80"
                     : "=a"(r)
                     : "a"(op), "D"(slot), "S"(w0), "d"(w1), "r"(r10), "r"(r8)
                     : "memory", "rcx", "r11");
    return r;
}
long wsend(long slot, long w0, long w1, long w2, long w3) { return wsend_op(WSYS_SEND, slot, w0, w1, w2, w3); }
long wsend_nb(long slot, long w0, long w1, long w2, long w3) { return wsend_op(WSYS_SEND_NB, slot, w0, w1, w2, w3); }

long wcall(long slot, long reply_slot, long w0, long w1, long w2, long w3, struct wrecv *out) {
    long r, badge, o0, o1, o2, o3;
    register long r10 __asm__("r10") = w2;
    register long r8 __asm__("r8") = w3;
    register long r9 __asm__("r9") = reply_slot;
    __asm__ volatile("int $0x80"
                     : "=a"(r), "=D"(badge), "=S"(o0), "=d"(o1), "+r"(r10), "+r"(r8)
                     : "a"(WSYS_CALL), "D"(slot), "S"(w0), "d"(w1), "r"(r9)
                     : "memory", "rcx", "r11");
    o2 = r10;
    o3 = r8;
    if (out) {
        out->r = r;
        out->badge = badge;
        out->w0 = o0;
        out->w1 = o1;
        out->w2 = o2;
        out->w3 = o3;
    }
    return r;
}

struct wrecv wrecv(long slot) {
    struct wrecv o;
    long r, badge, w0, w1;
    register long r10 __asm__("r10");
    register long r8 __asm__("r8");
    __asm__ volatile("int $0x80"
                     : "=a"(r), "=D"(badge), "=S"(w0), "=d"(w1), "=r"(r10), "=r"(r8)
                     : "a"(WSYS_RECV), "D"(slot)
                     : "memory", "rcx", "r11");
    o.r = r;
    o.badge = badge;
    o.w0 = w0;
    o.w1 = w1;
    o.w2 = r10;
    o.w3 = r8;
    return o;
}

/* ---- marshalling ------------------------------------------------------- */

static volatile uint64_t *const hdr = (volatile uint64_t *)APP_BUF;
static uint8_t *const data = (uint8_t *)(APP_BUF + BUF_DATA_OFF);

long lsys6(long nr, long a0, long a1, long a2, long a3, long a4, long a5) {
    struct wrecv o;
    hdr[0] = (uint64_t)a0;
    hdr[1] = (uint64_t)a1;
    hdr[2] = (uint64_t)a2;
    hdr[3] = (uint64_t)a3;
    hdr[4] = (uint64_t)a4;
    hdr[5] = (uint64_t)a5;
    long r = wcall(SLOT_PERSONALITY, SLOT_REPLY, nr, a0, a1, a2, &o);
    if (r != 0)
        return -ENOSYS; /* the kernel refused the call itself */
    return o.w0;
}
static long lsys(long nr, long a0, long a1, long a2) { return lsys6(nr, a0, a1, a2, 0, 0, 0); }

long sys_read(int fd, void *buf, size_t n) {
    if (n > BUF_DATA_MAX)
        n = BUF_DATA_MAX;
    long r = lsys(NR_read, fd, 0, (long)n);
    if (r > 0)
        memcpy(buf, data, (size_t)r);
    return r;
}
long sys_write(int fd, const void *buf, size_t n) {
    if (n > BUF_DATA_MAX)
        n = BUF_DATA_MAX;
    memcpy(data, buf, n);
    return lsys(NR_write, fd, 0, (long)n);
}
long sys_open(const char *path, int flags) {
    size_t n = strlen(path);
    if (n >= 256)
        return -EINVAL;
    memcpy(data, path, n + 1);
    return lsys(NR_open, 0, flags, (long)n);
}
long sys_close(int fd) { return lsys(NR_close, fd, 0, 0); }
long sys_socket(int domain, int type, int proto) { return lsys(NR_socket, domain, type, proto); }
long sys_bind(int fd, const struct sockaddr_in *a) {
    memcpy(data, a, sizeof *a);
    return lsys(NR_bind, fd, 0, sizeof *a);
}
long sys_listen(int fd, int backlog) { return lsys(NR_listen, fd, backlog, 0); }
long sys_accept(int fd) { return lsys(NR_accept, fd, 0, 0); }
long sys_connect(int fd, const struct sockaddr_in *a) {
    memcpy(data, a, sizeof *a);
    return lsys(NR_connect, fd, 0, sizeof *a);
}
long sys_epoll_create1(int flags) { return lsys(NR_epoll_create1, flags, 0, 0); }
long sys_epoll_ctl(int epfd, int op, int fd, const struct epoll_event *ev) {
    if (ev)
        memcpy(data, ev, sizeof *ev);
    else
        memset(data, 0, sizeof *ev);
    return lsys(NR_epoll_ctl, epfd, op, fd);
}
long sys_epoll_wait(int epfd, struct epoll_event *evs, int max, int timeout) {
    if (max < 0)
        return -EINVAL;
    if ((size_t)max > BUF_DATA_MAX / sizeof(struct epoll_event))
        max = (int)(BUF_DATA_MAX / sizeof(struct epoll_event));
    long r = lsys6(NR_epoll_wait, epfd, 0, max, timeout, 0, 0);
    if (r > 0)
        memcpy(evs, data, (size_t)r * sizeof(struct epoll_event));
    return r;
}
void sys_yield(void) { wsys(WSYS_YIELD, 0, 0, 0); }

void _exit(int code) {
    lsys(NR_exit_group, code, 0, 0); /* the server releases our fds */
    wsys(WSYS_EXIT, code, 0, 0);
    for (;;)
        ;
}

/* ---- helpers ----------------------------------------------------------- */

void *memcpy(void *d, const void *s, size_t n) {
    uint8_t *dp = d;
    const uint8_t *sp = s;
    while (n--)
        *dp++ = *sp++;
    return d;
}
void *memset(void *d, int c, size_t n) {
    uint8_t *dp = d;
    while (n--)
        *dp++ = (uint8_t)c;
    return d;
}
int memcmp(const void *a, const void *b, size_t n) {
    const uint8_t *x = a, *y = b;
    for (; n; n--, x++, y++)
        if (*x != *y)
            return *x - *y;
    return 0;
}
size_t strlen(const char *s) {
    size_t n = 0;
    while (s[n])
        n++;
    return n;
}
int strcmp(const char *a, const char *b) {
    while (*a && *a == *b)
        a++, b++;
    return (unsigned char)*a - (unsigned char)*b;
}
int strncmp(const char *a, const char *b, size_t n) {
    for (; n; n--, a++, b++) {
        if (*a != *b)
            return (unsigned char)*a - (unsigned char)*b;
        if (!*a)
            return 0;
    }
    return 0;
}
char *strstr(const char *h, const char *nd) {
    size_t n = strlen(nd);
    for (; *h; h++)
        if (!strncmp(h, nd, n))
            return (char *)h;
    return 0;
}
size_t utoa(unsigned long v, char *out) {
    char tmp[24];
    size_t n = 0;
    do {
        tmp[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    for (size_t i = 0; i < n; i++)
        out[i] = tmp[n - 1 - i];
    return n;
}
long atol_n(const char *s, size_t n) {
    long v = 0;
    for (size_t i = 0; i < n && s[i] >= '0' && s[i] <= '9'; i++)
        v = v * 10 + (s[i] - '0');
    return v;
}
uint16_t htons(uint16_t v) { return (uint16_t)((v << 8) | (v >> 8)); }
void puts1(const char *s) { sys_write(1, s, strlen(s)); }
uint32_t fnv1a(const void *p, size_t n) {
    const uint8_t *b = p;
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) {
        h ^= b[i];
        h *= 16777619u;
    }
    return h;
}
