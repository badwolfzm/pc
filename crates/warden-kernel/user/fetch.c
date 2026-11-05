/* An HTTP/1.0 client: N_REQUESTS GETs over fresh loopback connections,
 * each response checked byte for byte against the site fixture. Exits 0
 * only when every response was right. Runs at the same time as httpd and
 * the rogue, in its own address space, with only a net.connect grant. */
#include "libw.h"
#include "site.h"

static char rbuf[16384];
static uint8_t expect_blob[SITE_BLOB_LEN];

struct want {
    const char *path;
    int status;
    const uint8_t *body;
    size_t len;
};

static long connect_retry(void) {
    for (int tries = 0; tries < 2000; tries++) {
        long fd = sys_socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0)
            return fd;
        struct sockaddr_in a = {AF_INET, htons(HTTP_PORT), 0x0100007f, {0}};
        long r = sys_connect((int)fd, &a);
        if (r == 0)
            return fd;
        sys_close((int)fd);
        if (r != -ECONNREFUSED)
            return r;
        sys_yield(); /* httpd is not listening yet */
    }
    return -ECONNREFUSED;
}

/* One request; 1 when the response matches, 0 otherwise. */
static int one(const struct want *w) {
    long fd = connect_retry();
    if (fd < 0)
        return 0;
    char req[256];
    size_t n = 0;
    const char *p = "GET ";
    memcpy(req, p, 4);
    n += 4;
    memcpy(req + n, w->path, strlen(w->path));
    n += strlen(w->path);
    p = " HTTP/1.0\r\nHost: warden\r\n\r\n";
    memcpy(req + n, p, strlen(p));
    n += strlen(p);
    if (sys_write((int)fd, req, n) != (long)n) {
        sys_close((int)fd);
        return 0;
    }
    size_t got = 0;
    for (;;) {
        long r = sys_read((int)fd, rbuf + got, sizeof rbuf - 1 - got);
        if (r < 0) {
            sys_close((int)fd);
            return 0;
        }
        if (r == 0)
            break;
        got += (size_t)r;
        if (got >= sizeof rbuf - 1)
            break;
    }
    sys_close((int)fd);
    rbuf[got] = 0;
    if (got < 12 || strncmp(rbuf, "HTTP/1.0 ", 9))
        return 0;
    long status = atol_n(rbuf + 9, 3);
    char *cl = strstr(rbuf, "Content-Length: ");
    char *end = strstr(rbuf, "\r\n\r\n");
    if (!cl || !end || status != w->status)
        return 0;
    long len = atol_n(cl + 16, 10);
    size_t body_off = (size_t)(end + 4 - rbuf);
    if (len != (long)w->len || got - body_off != w->len)
        return 0;
    return memcmp(rbuf + body_off, w->body, w->len) == 0;
}

int main(void) {
    for (size_t i = 0; i < SITE_BLOB_LEN; i++)
        expect_blob[i] = site_blob_byte(i);
    const struct want wants[4] = {
        {"/index.html", 200, (const uint8_t *)SITE_INDEX, sizeof SITE_INDEX - 1},
        {"/style.css", 200, (const uint8_t *)SITE_CSS, sizeof SITE_CSS - 1},
        {"/blob.bin", 200, expect_blob, SITE_BLOB_LEN},
        {"/missing.html", 404, (const uint8_t *)"", 0},
    };
    int ok = 0;
    for (int i = 0; i < N_REQUESTS; i++)
        ok += one(&wants[i % 4]);
    char line[80];
    size_t n = 0;
    const char *p = "fetch: ";
    memcpy(line, p, strlen(p));
    n += strlen(p);
    n += utoa((unsigned long)ok, line + n);
    line[n++] = '/';
    n += utoa(N_REQUESTS, line + n);
    p = " responses correct\n";
    memcpy(line + n, p, strlen(p));
    n += strlen(p);
    sys_write(1, line, n);
    return ok == N_REQUESTS ? 0 : 1;
}
