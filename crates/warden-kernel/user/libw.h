/* A very small libc for Warden Phase 2b programs. Linux-shaped calls are
 * marshalled into the IPC buffer and sent to the personality server with one
 * WSYS_CALL each. Nothing here touches the FPU or SSE: the kernel does not
 * save that state yet, so every program is built -mgeneral-regs-only. */
#ifndef LIBW_H
#define LIBW_H
#include <stddef.h>
#include <stdint.h>
#include "warden_abi.h"

/* Linux x86-64 syscall numbers the personality server implements. */
#define NR_read 0
#define NR_write 1
#define NR_open 2
#define NR_close 3
#define NR_sched_yield 24
#define NR_socket 41
#define NR_connect 42
#define NR_accept 43
#define NR_bind 49
#define NR_listen 50
#define NR_exit_group 231
#define NR_epoll_wait 232
#define NR_epoll_ctl 233
#define NR_epoll_create1 291

#define ENOENT 2
#define EBADF 9
#define EAGAIN 11
#define ENOMEM 12
#define EACCES 13
#define EFAULT 14
#define EINVAL 22
#define ENFILE 23
#define EMFILE 24
#define ENOSYS 38
#define EPIPE 32
#define ENOTSOCK 88
#define EADDRINUSE 98
#define EISCONN 106
#define ENOTCONN 107
#define ECONNREFUSED 111

#define AF_INET 2
#define SOCK_STREAM 1
#define O_RDONLY 0
#define EPOLLIN 1u
#define EPOLLOUT 4u
#define EPOLL_CTL_ADD 1
#define EPOLL_CTL_DEL 2
#define EPOLL_CTL_MOD 3

struct sockaddr_in {
    uint16_t sin_family;
    uint16_t sin_port; /* big endian, as on Linux */
    uint32_t sin_addr;
    uint8_t sin_zero[8];
};

struct epoll_event {
    uint32_t events;
    uint64_t data;
} __attribute__((packed));

struct wrecv {
    long r, badge, w0, w1, w2, w3;
};

/* Raw kernel syscalls. */
long wsys(long nr, long a, long b, long c);
long wsend(long slot, long w0, long w1, long w2, long w3);
long wsend_nb(long slot, long w0, long w1, long w2, long w3);
long wcall(long slot, long reply_slot, long w0, long w1, long w2, long w3, struct wrecv *out);
struct wrecv wrecv(long slot);

/* Marshalled Linux-shaped calls (negative errno on failure). */
long sys_read(int fd, void *buf, size_t n);
long sys_write(int fd, const void *buf, size_t n);
long sys_open(const char *path, int flags);
long sys_close(int fd);
long sys_socket(int domain, int type, int proto);
long sys_bind(int fd, const struct sockaddr_in *a);
long sys_listen(int fd, int backlog);
long sys_accept(int fd);
long sys_connect(int fd, const struct sockaddr_in *a);
long sys_epoll_create1(int flags);
long sys_epoll_ctl(int epfd, int op, int fd, const struct epoll_event *ev);
long sys_epoll_wait(int epfd, struct epoll_event *evs, int max, int timeout);
void sys_yield(void);
void _exit(int code) __attribute__((noreturn));

/* Raw personality call with explicit header arguments; the rogue uses it to
 * send what a well-behaved libc never would. */
long lsys6(long nr, long a0, long a1, long a2, long a3, long a4, long a5);

/* Helpers. */
void *memcpy(void *d, const void *s, size_t n);
void *memset(void *d, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
char *strstr(const char *h, const char *n);
size_t utoa(unsigned long v, char *out); /* decimal, returns length, no NUL */
long atol_n(const char *s, size_t n);
uint16_t htons(uint16_t v);
void puts1(const char *s); /* write(1, s, strlen(s)) */
uint32_t fnv1a(const void *p, size_t n);

#endif
