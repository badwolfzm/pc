/* A small static HTTP/1.0 server: epoll over one listening socket and its
 * connections, files read through the fs.read grant. It exits 0 after
 * N_REQUESTS responses. It never sees the kernel console: its only output
 * is write(1), which the personality server forwards. */
#include "libw.h"

#define MAX_CONN 8
struct conn {
    int fd;
    char req[512];
    size_t len;
};
static struct conn conns[MAX_CONN];
static char body[12288];
static char resp[12288 + 256];

static const char *content_type(const char *path) {
    size_t n = strlen(path);
    if (n >= 5 && !strcmp(path + n - 5, ".html"))
        return "text/html";
    if (n >= 4 && !strcmp(path + n - 4, ".css"))
        return "text/css";
    return "application/octet-stream";
}

static void write_all(int fd, const char *p, size_t n) {
    while (n) {
        long r = sys_write(fd, p, n);
        if (r <= 0)
            return;
        p += r;
        n -= (size_t)r;
    }
}

/* Build and send the response for one complete request. */
static void respond(struct conn *c) {
    char path[300] = "/site";
    const char *q = c->req;
    long status = 400;
    size_t blen = 0;
    if (!strncmp(q, "GET ", 4)) {
        q += 4;
        size_t i = 0;
        while (q[i] && q[i] != ' ' && q[i] != '\r' && i < 250)
            i++;
        if (i > 0 && q[0] == '/' && !strstr(q, "..")) {
            memcpy(path + 5, q, i);
            path[5 + i] = 0;
            long fd = sys_open(path, O_RDONLY);
            if (fd >= 0) {
                status = 200;
                for (;;) {
                    long r = sys_read((int)fd, body + blen, sizeof body - blen);
                    if (r <= 0)
                        break;
                    blen += (size_t)r;
                }
                sys_close((int)fd);
            } else if (fd == -ENOENT) {
                status = 404;
            } else {
                status = 403;
            }
        }
    }
    size_t n = 0;
    const char *line = status == 200 ? "HTTP/1.0 200 OK\r\n"
                       : status == 404 ? "HTTP/1.0 404 Not Found\r\n"
                       : status == 403 ? "HTTP/1.0 403 Forbidden\r\n"
                                       : "HTTP/1.0 400 Bad Request\r\n";
    memcpy(resp, line, strlen(line));
    n += strlen(line);
    const char *ct = "Content-Type: ";
    memcpy(resp + n, ct, strlen(ct));
    n += strlen(ct);
    const char *t = status == 200 ? content_type(path) : "text/plain";
    memcpy(resp + n, t, strlen(t));
    n += strlen(t);
    const char *cl = "\r\nContent-Length: ";
    memcpy(resp + n, cl, strlen(cl));
    n += strlen(cl);
    n += utoa(blen, resp + n);
    memcpy(resp + n, "\r\n\r\n", 4);
    n += 4;
    memcpy(resp + n, body, blen);
    n += blen;
    write_all(c->fd, resp, n);
}

int main(void) {
    long ls = sys_socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a = {AF_INET, htons(HTTP_PORT), 0, {0}};
    if (ls < 0 || sys_bind((int)ls, &a) < 0 || sys_listen((int)ls, 8) < 0) {
        puts1("httpd: cannot listen\n");
        return 1;
    }
    long ep = sys_epoll_create1(0);
    struct epoll_event ev = {EPOLLIN, (uint64_t)ls};
    if (ep < 0 || sys_epoll_ctl((int)ep, EPOLL_CTL_ADD, (int)ls, &ev) < 0) {
        puts1("httpd: epoll failed\n");
        return 1;
    }
    for (int i = 0; i < MAX_CONN; i++)
        conns[i].fd = -1;
    puts1("httpd: listening on :8080\n");
    int served = 0;
    while (served < N_REQUESTS) {
        struct epoll_event evs[MAX_CONN + 1];
        long n = sys_epoll_wait((int)ep, evs, MAX_CONN + 1, -1);
        if (n < 0) {
            puts1("httpd: epoll_wait failed\n");
            return 1;
        }
        for (long i = 0; i < n; i++) {
            if ((long)evs[i].data == ls) {
                long c = sys_accept((int)ls);
                if (c < 0)
                    continue;
                int slot = -1;
                for (int j = 0; j < MAX_CONN; j++)
                    if (conns[j].fd < 0) {
                        slot = j;
                        break;
                    }
                if (slot < 0) {
                    sys_close((int)c);
                    continue;
                }
                conns[slot].fd = (int)c;
                conns[slot].len = 0;
                struct epoll_event cev = {EPOLLIN, (uint64_t)c};
                sys_epoll_ctl((int)ep, EPOLL_CTL_ADD, (int)c, &cev);
                continue;
            }
            struct conn *c = 0;
            for (int j = 0; j < MAX_CONN; j++)
                if (conns[j].fd == (int)evs[i].data)
                    c = &conns[j];
            if (!c)
                continue;
            long r = sys_read(c->fd, c->req + c->len, sizeof c->req - 1 - c->len);
            if (r <= 0 || c->len + (size_t)r >= sizeof c->req - 1) {
                sys_epoll_ctl((int)ep, EPOLL_CTL_DEL, c->fd, 0);
                sys_close(c->fd);
                c->fd = -1;
                continue;
            }
            c->len += (size_t)r;
            c->req[c->len] = 0;
            if (strstr(c->req, "\r\n\r\n")) {
                respond(c);
                sys_epoll_ctl((int)ep, EPOLL_CTL_DEL, c->fd, 0);
                sys_close(c->fd);
                c->fd = -1;
                served++;
            }
        }
    }
    char line[64];
    size_t n = 0;
    const char *p = "httpd: served ";
    memcpy(line, p, strlen(p));
    n += strlen(p);
    n += utoa((unsigned long)served, line + n);
    p = " requests, exiting\n";
    memcpy(line + n, p, strlen(p));
    n += strlen(p);
    sys_write(1, line, n);
    return 0;
}
