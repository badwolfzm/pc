/* Warden Linux personality server, Phase 2b.
 *
 * A ring-3 program, like the applications it serves. It receives one
 * marshalled Linux syscall per WSYS_CALL on its endpoint, identifies the
 * caller by the badge the kernel put on that caller's capability (never by
 * anything the caller wrote), performs the call against per-client state,
 * and replies on the caller's reply endpoint with a non-blocking send.
 *
 * What it implements: read/write/open/close on a read-only in-memory site
 * (fs.read), AF_INET stream sockets over an in-server loopback (net.listen,
 * net.connect), epoll, exit_group, sched_yield. A call that cannot complete
 * yet (accept with an empty backlog, read on an empty stream, epoll_wait with
 * nothing ready, write into a full ring) is parked as the client's one
 * pending operation and retried after every state change; the client stays
 * blocked in the kernel on its reply endpoint meanwhile.
 *
 * Not real: there is no NIC, no TCP, no VFS service yet; the loopback and
 * the site live in this program's own memory. The permission table below
 * stands in for the manifest until the policy service exists.
 */
#include "libw.h"
#include "site.h"

#define NFD 16
#define MAX_OPEN 32
#define MAX_LISTEN 4
#define MAX_STREAM 8
#define MAX_EPOLL 16
#define EP_ITEMS 8
#define BACKLOG 8
#define RING 32768

enum { FD_NONE, FD_STD, FD_FILE, FD_LISTEN, FD_STREAM, FD_EPOLL };
enum { PERM_FS_READ = 1, PERM_NET_LISTEN = 2, PERM_NET_CONNECT = 4 };

struct fd {
    int type, idx;
};
struct client {
    int state; /* 0 never connected, 1 alive, 2 exited */
    unsigned perms;
    const char *name;
    struct fd fds[NFD];
    int pending;
    long pnr, pargs[6];
    uint8_t *buf;
};
struct ofile {
    int used, file;
    size_t off;
};
struct listener {
    int used, owner;
    uint16_t port;
    int backlog[BACKLOG];
    int nbacklog;
};
struct stream {
    int used, owner, peer, connected, peer_closed;
    uint16_t bound_port;
    uint8_t rx[RING];
    unsigned head, tail; /* rx ring: data the owner may read */
};
struct epoll {
    int used, owner, n;
    struct {
        int fd;
        uint32_t events;
        uint64_t data;
    } items[EP_ITEMS];
};
struct site_file {
    const char *path;
    const uint8_t *data;
    size_t len;
};

/* The manifest stand-in: who may do what. Client ids are kernel badges. */
static struct client clients[MAX_CLIENTS + 1];
static const struct {
    const char *name;
    unsigned perms;
} manifest[MAX_CLIENTS + 1] = {
    {"?", 0},
    {"httpd", PERM_FS_READ | PERM_NET_LISTEN},
    {"fetch", PERM_NET_CONNECT},
    {"rogue", 0},
    /* Linux programs, forwarded by the kernel (same protocol, same rules). */
    {"lhttpd", PERM_FS_READ | PERM_NET_LISTEN},
    {"lfetch", PERM_NET_CONNECT},
    {"lrogue", 0},
};
#define N_EXPECTED_CLIENTS 6

static struct ofile ofiles[MAX_OPEN];
static struct listener listeners[MAX_LISTEN];
static struct stream streams[MAX_STREAM];
static struct epoll epolls[MAX_EPOLL];
static uint8_t blob[SITE_BLOB_LEN];
static struct site_file site[3];
static unsigned long stat_connections, stat_exits, stat_enosys, stat_calls;

static void console(const char *s, size_t n) {
    while (n) {
        size_t k = n > 1024 ? 1024 : n;
        wsys(WSYS_CONSOLE_WRITE, SLOT_CONSOLE, (long)s, (long)k);
        s += k;
        n -= k;
    }
}
static void logline(const char *a, const char *b) {
    console(a, strlen(a));
    console(b, strlen(b));
    console("\n", 1);
}

/* ---- per-client tables --------------------------------------------------- */

static struct fd *fd_of(struct client *c, long fd) {
    if (fd < 0 || fd >= NFD || c->fds[fd].type == FD_NONE)
        return 0;
    return &c->fds[fd];
}
static int fd_alloc(struct client *c, int type, int idx) {
    for (int i = 3; i < NFD; i++)
        if (c->fds[i].type == FD_NONE) {
            c->fds[i].type = type;
            c->fds[i].idx = idx;
            return i;
        }
    return -EMFILE;
}
static int stream_alloc(int owner) {
    for (int i = 0; i < MAX_STREAM; i++)
        if (!streams[i].used) {
            memset(&streams[i], 0, sizeof streams[i]);
            streams[i].used = 1;
            streams[i].owner = owner;
            streams[i].peer = -1;
            return i;
        }
    return -1;
}
static unsigned ring_used(const struct stream *s) { return s->head - s->tail; }
static void stream_release(int si) {
    struct stream *s = &streams[si];
    if (s->peer >= 0 && streams[s->peer].used) {
        streams[s->peer].peer_closed = 1;
        streams[s->peer].peer = -1;
    }
    s->used = 0;
}
static void fd_close(struct client *c, struct fd *f) {
    switch (f->type) {
    case FD_FILE:
        ofiles[f->idx].used = 0;
        break;
    case FD_STREAM:
        stream_release(f->idx);
        break;
    case FD_LISTEN: {
        struct listener *l = &listeners[f->idx];
        for (int i = 0; i < l->nbacklog; i++)
            stream_release(l->backlog[i]);
        l->used = 0;
        break;
    }
    case FD_EPOLL:
        epolls[f->idx].used = 0;
        break;
    }
    f->type = FD_NONE;
    (void)c;
}

/* ---- the calls ------------------------------------------------------------
 * Each returns a result to reply with, or PENDING to park the call. Data for
 * the client goes into its buffer at BUF_DATA_OFF; lengths are clamped to
 * BUF_DATA_MAX before any copy, whatever the header says. */
#define PENDING ((long)0x7fffffffffffffffL)

static int readable(struct client *c, struct fd *f) {
    switch (f->type) {
    case FD_STD:
    case FD_FILE:
        return 1;
    case FD_LISTEN:
        return listeners[f->idx].nbacklog > 0;
    case FD_STREAM:
        return ring_used(&streams[f->idx]) > 0 || streams[f->idx].peer_closed || streams[f->idx].peer < 0;
    }
    (void)c;
    return 0;
}
static int writable(struct fd *f) {
    if (f->type == FD_STD)
        return 1;
    if (f->type != FD_STREAM)
        return 0;
    struct stream *s = &streams[f->idx];
    return s->peer < 0 || ring_used(&streams[s->peer]) < RING;
}

static long do_read(struct client *c, long fd, size_t len) {
    struct fd *f = fd_of(c, fd);
    if (!f)
        return -EBADF;
    uint8_t *out = c->buf + BUF_DATA_OFF;
    if (len > BUF_DATA_MAX)
        len = BUF_DATA_MAX;
    switch (f->type) {
    case FD_STD:
        return 0; /* stdin is always at EOF */
    case FD_FILE: {
        struct ofile *o = &ofiles[f->idx];
        const struct site_file *sf = &site[o->file];
        size_t left = sf->len - o->off;
        if (len > left)
            len = left;
        memcpy(out, sf->data + o->off, len);
        o->off += len;
        return (long)len;
    }
    case FD_STREAM: {
        struct stream *s = &streams[f->idx];
        unsigned avail = ring_used(s);
        if (avail == 0) {
            if (s->peer_closed || s->peer < 0 || !s->connected)
                return 0;
            return PENDING;
        }
        if (len > avail)
            len = avail;
        for (size_t i = 0; i < len; i++)
            out[i] = s->rx[(s->tail + i) % RING];
        s->tail += (unsigned)len;
        return (long)len;
    }
    default:
        return -EINVAL;
    }
}

static long do_write(struct client *c, long fd, size_t len) {
    struct fd *f = fd_of(c, fd);
    if (!f)
        return -EBADF;
    if (len > BUF_DATA_MAX)
        return -EFAULT; /* the header names bytes that are not in the buffer */
    const uint8_t *in = c->buf + BUF_DATA_OFF;
    switch (f->type) {
    case FD_STD: {
        console("[", 1);
        console(c->name, strlen(c->name));
        console("] ", 2);
        console((const char *)in, len);
        return (long)len;
    }
    case FD_STREAM: {
        struct stream *s = &streams[f->idx];
        if (!s->connected)
            return -ENOTCONN;
        if (s->peer < 0)
            return -EPIPE;
        struct stream *p = &streams[s->peer];
        unsigned space = RING - ring_used(p);
        if (space == 0)
            return PENDING;
        if (len > space)
            len = space;
        for (size_t i = 0; i < len; i++)
            p->rx[(p->head + i) % RING] = in[i];
        p->head += (unsigned)len;
        return (long)len;
    }
    default:
        return -EINVAL;
    }
}

static long do_open(struct client *c, int flags, size_t n) {
    if (!(c->perms & PERM_FS_READ))
        return -EACCES;
    if (flags != O_RDONLY)
        return -EACCES; /* the grant is read-only */
    if (n >= 256 || n >= BUF_DATA_MAX)
        return -EINVAL;
    char path[256];
    memcpy(path, c->buf + BUF_DATA_OFF, n);
    path[n] = 0;
    for (size_t i = 0; i < n; i++)
        if (path[i] == 0)
            return -EINVAL;
    int file = -1;
    for (int i = 0; i < 3; i++)
        if (!strcmp(path, site[i].path))
            file = i;
    if (file < 0)
        return -ENOENT;
    int oi = -1;
    for (int i = 0; i < MAX_OPEN; i++)
        if (!ofiles[i].used) {
            oi = i;
            break;
        }
    if (oi < 0)
        return -ENFILE;
    int fd = fd_alloc(c, FD_FILE, oi);
    if (fd < 0)
        return fd;
    ofiles[oi].used = 1;
    ofiles[oi].file = file;
    ofiles[oi].off = 0;
    return fd;
}

static long do_close(struct client *c, long fd) {
    struct fd *f = fd_of(c, fd);
    if (!f)
        return -EBADF;
    if (f->type == FD_STD) {
        f->type = FD_NONE;
        return 0;
    }
    fd_close(c, f);
    return 0;
}

static long do_socket(struct client *c, long domain, long type) {
    if (!(c->perms & (PERM_NET_LISTEN | PERM_NET_CONNECT)))
        return -EACCES;
    if (domain != AF_INET || type != SOCK_STREAM)
        return -EINVAL;
    int si = stream_alloc((int)(c - clients));
    if (si < 0)
        return -ENOMEM;
    int fd = fd_alloc(c, FD_STREAM, si);
    if (fd < 0) {
        streams[si].used = 0;
        return fd;
    }
    return fd;
}

static int parse_port(struct client *c, size_t len, uint16_t *port) {
    struct sockaddr_in a;
    if (len != sizeof a || len > BUF_DATA_MAX)
        return -EINVAL;
    memcpy(&a, c->buf + BUF_DATA_OFF, sizeof a);
    if (a.sin_family != AF_INET)
        return -EINVAL;
    *port = htons(a.sin_port);
    return 0;
}

static long do_bind(struct client *c, long fd, size_t len) {
    struct fd *f = fd_of(c, fd);
    if (!f)
        return -EBADF;
    if (f->type != FD_STREAM)
        return -ENOTSOCK;
    if (!(c->perms & PERM_NET_LISTEN))
        return -EACCES;
    uint16_t port;
    int e = parse_port(c, len, &port);
    if (e)
        return e;
    for (int i = 0; i < MAX_LISTEN; i++)
        if (listeners[i].used && listeners[i].port == port)
            return -EADDRINUSE;
    streams[f->idx].bound_port = port;
    return 0;
}

static long do_listen(struct client *c, long fd) {
    struct fd *f = fd_of(c, fd);
    if (!f)
        return -EBADF;
    if (f->type != FD_STREAM)
        return -ENOTSOCK;
    if (!(c->perms & PERM_NET_LISTEN))
        return -EACCES;
    struct stream *s = &streams[f->idx];
    if (s->bound_port == 0 || s->connected)
        return -EINVAL;
    int li = -1;
    for (int i = 0; i < MAX_LISTEN; i++)
        if (!listeners[i].used) {
            li = i;
            break;
        }
    if (li < 0)
        return -ENOMEM;
    memset(&listeners[li], 0, sizeof listeners[li]);
    listeners[li].used = 1;
    listeners[li].owner = (int)(c - clients);
    listeners[li].port = s->bound_port;
    s->used = 0;
    f->type = FD_LISTEN;
    f->idx = li;
    return 0;
}

static long do_accept(struct client *c, long fd) {
    struct fd *f = fd_of(c, fd);
    if (!f)
        return -EBADF;
    if (f->type != FD_LISTEN)
        return -EINVAL;
    struct listener *l = &listeners[f->idx];
    if (l->nbacklog == 0)
        return PENDING;
    int si = l->backlog[0];
    int nfd = fd_alloc(c, FD_STREAM, si);
    if (nfd < 0)
        return nfd; /* the connection stays queued */
    for (int i = 1; i < l->nbacklog; i++)
        l->backlog[i - 1] = l->backlog[i];
    l->nbacklog--;
    stat_connections++;
    return nfd;
}

static long do_connect(struct client *c, long fd, size_t len) {
    struct fd *f = fd_of(c, fd);
    if (!f)
        return -EBADF;
    if (f->type != FD_STREAM)
        return -ENOTSOCK;
    if (!(c->perms & PERM_NET_CONNECT))
        return -EACCES;
    struct stream *s = &streams[f->idx];
    if (s->connected)
        return -EISCONN;
    uint16_t port;
    int e = parse_port(c, len, &port);
    if (e)
        return e;
    struct listener *l = 0;
    for (int i = 0; i < MAX_LISTEN; i++)
        if (listeners[i].used && listeners[i].port == port)
            l = &listeners[i];
    if (!l || l->nbacklog >= BACKLOG)
        return -ECONNREFUSED;
    int ti = stream_alloc(l->owner);
    if (ti < 0)
        return -ECONNREFUSED;
    struct stream *t = &streams[ti];
    t->peer = f->idx;
    t->connected = 1;
    s->peer = ti;
    s->connected = 1;
    l->backlog[l->nbacklog++] = ti;
    return 0;
}

static long do_epoll_create(struct client *c) {
    int ei = -1;
    for (int i = 0; i < MAX_EPOLL; i++)
        if (!epolls[i].used) {
            ei = i;
            break;
        }
    if (ei < 0)
        return -ENOMEM;
    int fd = fd_alloc(c, FD_EPOLL, ei);
    if (fd < 0)
        return fd;
    memset(&epolls[ei], 0, sizeof epolls[ei]);
    epolls[ei].used = 1;
    epolls[ei].owner = (int)(c - clients);
    return fd;
}

static long do_epoll_ctl(struct client *c, long epfd, long op, long fd) {
    struct fd *ef = fd_of(c, epfd);
    if (!ef || !fd_of(c, fd))
        return -EBADF;
    if (ef->type != FD_EPOLL)
        return -EINVAL;
    struct epoll *e = &epolls[ef->idx];
    struct epoll_event ev;
    memcpy(&ev, c->buf + BUF_DATA_OFF, sizeof ev);
    int at = -1;
    for (int i = 0; i < e->n; i++)
        if (e->items[i].fd == fd)
            at = i;
    switch (op) {
    case EPOLL_CTL_ADD:
        if (at >= 0)
            return -EINVAL;
        if (e->n >= EP_ITEMS)
            return -ENOMEM;
        e->items[e->n].fd = (int)fd;
        e->items[e->n].events = ev.events;
        e->items[e->n].data = ev.data;
        e->n++;
        return 0;
    case EPOLL_CTL_MOD:
        if (at < 0)
            return -ENOENT;
        e->items[at].events = ev.events;
        e->items[at].data = ev.data;
        return 0;
    case EPOLL_CTL_DEL:
        if (at < 0)
            return -ENOENT;
        for (int i = at + 1; i < e->n; i++)
            e->items[i - 1] = e->items[i];
        e->n--;
        return 0;
    }
    return -EINVAL;
}

static long do_epoll_wait(struct client *c, long epfd, long max, long timeout) {
    struct fd *ef = fd_of(c, epfd);
    if (!ef)
        return -EBADF;
    if (ef->type != FD_EPOLL || max <= 0)
        return -EINVAL;
    if ((size_t)max > BUF_DATA_MAX / sizeof(struct epoll_event))
        max = (long)(BUF_DATA_MAX / sizeof(struct epoll_event));
    struct epoll *e = &epolls[ef->idx];
    struct epoll_event *out = (struct epoll_event *)(c->buf + BUF_DATA_OFF);
    long n = 0;
    for (int i = 0; i < e->n && n < max; i++) {
        struct fd *f = fd_of(c, e->items[i].fd);
        if (!f)
            continue;
        uint32_t got = 0;
        if ((e->items[i].events & EPOLLIN) && readable(c, f))
            got |= EPOLLIN;
        if ((e->items[i].events & EPOLLOUT) && writable(f))
            got |= EPOLLOUT;
        if (got) {
            struct epoll_event ev = {got, e->items[i].data};
            memcpy(&out[n], &ev, sizeof ev);
            n++;
        }
    }
    if (n == 0 && timeout != 0)
        return PENDING;
    return n;
}

static long do_exit_group(struct client *c) {
    for (int i = 0; i < NFD; i++)
        if (c->fds[i].type != FD_NONE)
            fd_close(c, &c->fds[i]);
    c->state = 2;
    stat_exits++;
    return 0;
}

static long dispatch(struct client *c, long nr, const long *a) {
    switch (nr) {
    case NR_read:
        return do_read(c, a[0], (size_t)a[2]);
    case NR_write:
        return do_write(c, a[0], (size_t)a[2]);
    case NR_open:
    case NR_openat: /* the kernel marshals openat like open */
        return do_open(c, (int)a[1], (size_t)a[2]);
    case NR_close:
        return do_close(c, a[0]);
    case NR_sched_yield:
        return 0;
    case NR_socket:
        return do_socket(c, a[0], a[1]);
    case NR_connect:
        return do_connect(c, a[0], (size_t)a[2]);
    case NR_accept:
    case NR_accept4:
        return do_accept(c, a[0]);
    case NR_bind:
        return do_bind(c, a[0], (size_t)a[2]);
    case NR_listen:
        return do_listen(c, a[0]);
    case NR_exit_group:
        return do_exit_group(c);
    case NR_epoll_wait:
        return do_epoll_wait(c, a[0], a[2], a[3]);
    case NR_epoll_ctl:
        return do_epoll_ctl(c, a[0], a[1], a[2]);
    case NR_epoll_create1:
        return do_epoll_create(c);
    default:
        stat_enosys++;
        return -ENOSYS;
    }
}

/* Reply without ever blocking on a client. A client that is not waiting
 * (it died, or it never called properly) just loses its answer. */
static void reply(struct client *c, long val) {
    int id = (int)(c - clients);
    wsend_nb(SRV_SLOT_REPLY0 + id - 1, val, 0, 0, 0);
}

static void handle(struct client *c, long nr) {
    long a[6];
    volatile uint64_t *h = (volatile uint64_t *)c->buf;
    for (int i = 0; i < 6; i++)
        a[i] = (long)h[i]; /* read once; the client could race its own buffer */
    stat_calls++;
    long r = dispatch(c, nr, a);
    if (r == PENDING) {
        c->pending = 1;
        c->pnr = nr;
        memcpy(c->pargs, a, sizeof a);
        return;
    }
    reply(c, r);
}

static void retry_pending(void) {
    int progress = 1;
    while (progress) {
        progress = 0;
        for (int i = 1; i <= MAX_CLIENTS; i++) {
            struct client *c = &clients[i];
            if (c->state != 1 || !c->pending)
                continue;
            long r = dispatch(c, c->pnr, c->pargs);
            if (r == PENDING)
                continue;
            c->pending = 0;
            reply(c, r);
            progress = 1;
        }
    }
}

int main(void) {
    for (size_t i = 0; i < SITE_BLOB_LEN; i++)
        blob[i] = site_blob_byte(i);
    site[0] = (struct site_file){"/site/index.html", (const uint8_t *)SITE_INDEX, sizeof SITE_INDEX - 1};
    site[1] = (struct site_file){"/site/style.css", (const uint8_t *)SITE_CSS, sizeof SITE_CSS - 1};
    site[2] = (struct site_file){"/site/blob.bin", blob, SITE_BLOB_LEN};
    for (int i = 1; i <= MAX_CLIENTS; i++) {
        clients[i].perms = manifest[i].perms;
        clients[i].name = manifest[i].name;
        clients[i].buf = (uint8_t *)(SRV_BUF_BASE + (unsigned long)(i - 1) * BUF_SIZE);
        for (int fd = 0; fd < 3; fd++)
            clients[i].fds[fd].type = FD_STD;
    }
    logline("personality server up: ", "fs.read /site, loopback AF_INET streams, epoll");

    for (;;) {
        struct wrecv m = wrecv(SLOT_PERSONALITY);
        if (m.r != 0) {
            logline("personality: recv failed", "");
            wsys(WSYS_EXIT, 2, 0, 0);
        }
        long id = m.badge;
        if (id >= 1 && id <= MAX_CLIENTS) {
            struct client *c = &clients[id];
            if (c->state == 0)
                c->state = 1;
            if (c->state == 1 && !c->pending)
                handle(c, m.w0);
            /* a client with a pending call that calls again is confused;
             * it is blocked in the kernel, so this cannot happen normally */
        }
        retry_pending();
        if (stat_exits >= N_EXPECTED_CLIENTS)
            break;
    }
    char line[96];
    size_t n = 0;
    const char *p = "personality: ";
    memcpy(line, p, strlen(p));
    n += strlen(p);
    n += utoa(stat_calls, line + n);
    p = " calls, ";
    memcpy(line + n, p, strlen(p));
    n += strlen(p);
    n += utoa(stat_connections, line + n);
    p = " connections, ";
    memcpy(line + n, p, strlen(p));
    n += strlen(p);
    n += utoa(stat_enosys, line + n);
    p = " ENOSYS\n";
    memcpy(line + n, p, strlen(p));
    n += strlen(p);
    console(line, n);
    wsys(WSYS_LOG, SLOT_CONSOLE, T_SRV_CONNECTIONS, (long)stat_connections);
    wsys(WSYS_LOG, SLOT_CONSOLE, T_SRV_EXITS, (long)stat_exits);
    wsys(WSYS_EXIT, 0, 0, 0);
    return 0;
}
