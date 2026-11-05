/* A static HTTP/1.0 file server, as a plain Linux program: socket, bind
 * 8081, listen, epoll, accept4, read, openat under /site, read, write,
 * close. Serves two requests, then exits 0. Built with the ordinary Linux
 * toolchain flags (SSE allowed, linked at 0x400000) and run by the Warden
 * Linux personality; the descriptors live in the personality server. */
#include "sys.h"

static int serve(int c) {
    char req[512]; i64 n = sys3(SYS_read, c, req, sizeof req - 1);
    if (n <= 0) return 0;
    req[n] = 0;
    char path[200]; int pl = 0;
    const char *p = req;
    if (starts(p, "GET ")) {
        p += 4;
        const char *w = "/site"; while (*w) path[pl++] = *w++;
        while (*p && *p != ' ' && *p != '\r' && *p != '\n' && pl < 190) path[pl++] = *p++;
    }
    path[pl] = 0;
    int fd = (int)sys3(SYS_openat, -100, path, 0);
    if (fd < 0) {
        const char *r = "HTTP/1.0 404 Not Found\r\nContent-Length: 0\r\n\r\n";
        sys3(SYS_write, c, r, slen(r));
        return 1;
    }
    static char body[8192]; i64 size = 0;
    for (;;) {
        i64 k = sys3(SYS_read, fd, body + size, sizeof body - size);
        if (k <= 0) break;
        size += k;
        if (size >= (i64)sizeof body) break;
    }
    sys1(SYS_close, fd);
    char hdr[128]; int h = 0;
    const char *a = "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\nContent-Length: "; while (*a) hdr[h++] = *a++;
    char t[24]; int tn = 0; u64 v = (u64)size; do { t[tn++] = '0' + v % 10; v /= 10; } while (v); while (tn) hdr[h++] = t[--tn];
    hdr[h++] = '\r'; hdr[h++] = '\n'; hdr[h++] = '\r'; hdr[h++] = '\n';
    struct iovec iov[2] = { { hdr, (u64)h }, { body, (u64)size } };
    sys3(SYS_writev, c, iov, 2);
    return 1;
}

void prog(void) {
    int s = (int)sys3(SYS_socket, 2, 1, 0);
    int one = 1;
    sys6(SYS_setsockopt, s, 1, 2, (i64)&one, 4, 0);
    struct sockaddr_in sa = { 2, htons(8081), 0, {0} };
    i64 b = sys3(SYS_bind, s, &sa, sizeof sa);
    i64 l = sys2(SYS_listen, s, 4);
    check(140, (s >= 0 && b == 0 && l == 0) ? 1 : (b ? b : l));
    int ep = (int)sys1(SYS_epoll_create1, 0);
    struct epoll_event ev = { 1, (u64)s };
    sys4(SYS_epoll_ctl, ep, 1, s, &ev);
    int served = 0;
    while (served < 2) {
        struct epoll_event evs[4];
        i64 n = sys4(SYS_epoll_wait, ep, evs, 4, -1);
        if (n < 0) break;
        for (i64 i = 0; i < n; i++) {
            if (evs[i].data == (u64)s) {
                int c = (int)sys4(SYS_accept4, s, 0, 0, 0);
                if (c < 0) continue;
                struct epoll_event cev = { 1, (u64)c };
                sys4(SYS_epoll_ctl, ep, 1, c, &cev);
            } else {
                int c = (int)evs[i].data;
                served += serve(c);
                sys4(SYS_epoll_ctl, ep, 2, c, 0);
                sys1(SYS_close, c);
            }
        }
    }
    check(141, served);
    exit_group(0);
}
