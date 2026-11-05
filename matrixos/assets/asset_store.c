/*
 * asset_store.c — see asset_store.h.
 *
 * Linux only: needs openat2 (kernel 5.6+, glibc exposes no wrapper so it is
 * called through syscall(2)). WSL2's kernel is 5.15 or newer and has it.
 */
#define _GNU_SOURCE
#include "asset_store.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/openat2.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

/* ---------------------------------------------------------------- SHA-256 */

static const uint32_t K256[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2 };

#define ROTR(x,n) (((x) >> (n)) | ((x) << (32 - (n))))

static void sha256_block(uint32_t st[8], const uint8_t p[64]) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i)
        w[i] = ((uint32_t)p[4*i] << 24) | ((uint32_t)p[4*i+1] << 16) | ((uint32_t)p[4*i+2] << 8) | p[4*i+3];
    for (int i = 16; i < 64; ++i) {
        uint32_t s0 = ROTR(w[i-15],7) ^ ROTR(w[i-15],18) ^ (w[i-15] >> 3);
        uint32_t s1 = ROTR(w[i-2],17) ^ ROTR(w[i-2],19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    uint32_t a=st[0],b=st[1],c=st[2],d=st[3],e=st[4],f=st[5],g=st[6],h=st[7];
    for (int i = 0; i < 64; ++i) {
        uint32_t S1 = ROTR(e,6) ^ ROTR(e,11) ^ ROTR(e,25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = h + S1 + ch + K256[i] + w[i];
        uint32_t S0 = ROTR(a,2) ^ ROTR(a,13) ^ ROTR(a,22);
        uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = S0 + mj;
        h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    st[0]+=a; st[1]+=b; st[2]+=c; st[3]+=d; st[4]+=e; st[5]+=f; st[6]+=g; st[7]+=h;
}

void asset_sha256(const void *data, size_t len, uint8_t out[32]) {
    uint32_t st[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    const uint8_t *p = (const uint8_t *)data;
    size_t n = len;
    while (n >= 64) { sha256_block(st, p); p += 64; n -= 64; }
    uint8_t tail[128];
    memset(tail, 0, sizeof tail);
    memcpy(tail, p, n);
    tail[n] = 0x80;
    size_t tl = (n < 56) ? 64 : 128;
    uint64_t bits = (uint64_t)len * 8u;
    for (int i = 0; i < 8; ++i) tail[tl - 1 - i] = (uint8_t)(bits >> (8 * i));
    sha256_block(st, tail);
    if (tl == 128) sha256_block(st, tail + 64);
    for (int i = 0; i < 8; ++i) {
        out[4*i] = (uint8_t)(st[i] >> 24); out[4*i+1] = (uint8_t)(st[i] >> 16);
        out[4*i+2] = (uint8_t)(st[i] >> 8); out[4*i+3] = (uint8_t)st[i];
    }
}

void asset_hash_hex(const uint8_t h[32], char out[65]) {
    static const char hx[] = "0123456789abcdef";
    for (int i = 0; i < 32; ++i) { out[2*i] = hx[h[i] >> 4]; out[2*i+1] = hx[h[i] & 15]; }
    out[64] = 0;
}

static int hexval(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;   /* upper case is rejected on purpose: one canonical spelling per hash */
}

int asset_hash_parse(const char *hex, uint8_t out[32]) {
    if (!hex) return ASSET_ERR_ARG;
    for (int i = 0; i < 32; ++i) {
        int a = hexval((unsigned char)hex[2*i]), b = hexval((unsigned char)hex[2*i+1]);
        if (a < 0 || b < 0) return ASSET_ERR_FORMAT;
        out[i] = (uint8_t)((a << 4) | b);
    }
    if (hex[64] != 0) return ASSET_ERR_FORMAT;
    return ASSET_OK;
}

/* -------------------------------------------------------------- structures */

typedef struct { uint8_t hash[32]; uint32_t len; } chunk_ref;

typedef struct {
    uint32_t first_chunk, nchunks;
    uint64_t bytes;
    uint8_t *data;        /* resident buffer or NULL */
    uint8_t  priority;
    uint8_t  wanted, resident, inflight;
} mip_t;

typedef struct {
    int        used, closing;
    uint8_t    mhash[32];
    asset_kind kind;
    uint32_t   w, h, stride;
    uint32_t   nmips;
    mip_t      mips[ASSET_MAX_MIPS];
    chunk_ref *chunks;
    uint32_t   nchunks;
    uint32_t   generation;
    uint32_t   inflight_count;
} asset_t;

typedef struct { asset_handle h; uint32_t mip; uint8_t priority; uint64_t seq; } req_t;

typedef struct completion {
    struct completion *next;
    asset_handle h;
    uint32_t mip;
    uint8_t *data;
    uint64_t len;
    int rc;
} completion_t;

struct asset_store {
    int                dirfd;
    asset_store_config cfg;
    pthread_mutex_t    mu;
    pthread_cond_t     cv;
    pthread_t         *threads;
    unsigned           nthreads;
    int                stopping;

    asset_t           *assets;
    unsigned           nassets;

    req_t             *pending;
    unsigned           npending, cap_pending;
    uint64_t           seq;

    completion_t      *done_head, *done_tail;

    size_t             resident_bytes;
    asset_store_stats  stats;
};

static uint64_t now_ns(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

/* ------------------------------------------------------------------- jail */

int asset_store_open_beneath(asset_store *s, const char *relpath) {
    if (!s || !relpath || relpath[0] == '/' || relpath[0] == 0) { errno = EINVAL; return -1; }
    /* Over-long paths are refused outright, never truncated (security review: the
     * baseline VFS snprintf'ed into 512 bytes and silently cut the path). The
     * store never needs more than "chunks/xx/" + 64 hex, so 128 is generous. */
    if (strnlen(relpath, 129) > 128) { errno = ENAMETOOLONG; return -1; }
    struct open_how how;
    memset(&how, 0, sizeof how);
    how.flags   = O_RDONLY | O_CLOEXEC | O_NOCTTY;
    how.resolve = RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS | RESOLVE_NO_MAGICLINKS | RESOLVE_NO_XDEV;
    long fd = syscall(SYS_openat2, s->dirfd, relpath, &how, sizeof how);
    if (fd < 0) {
        if (errno == ELOOP || errno == EXDEV) {
            pthread_mutex_lock(&s->mu); s->stats.escapes_refused++; pthread_mutex_unlock(&s->mu);
        }
        return -1;
    }
    /* Belt and braces: the thing we opened must be a regular file. */
    struct stat st;
    if (fstat((int)fd, &st) != 0 || !S_ISREG(st.st_mode)) { close((int)fd); errno = ELOOP; return -1; }
    return (int)fd;
}

static int map_errno(void) {
    switch (errno) {
        case ELOOP: case EXDEV: case EAGAIN: return ASSET_ERR_ESCAPE;
        case ENOMEM: return ASSET_ERR_NOMEM;
        default: return ASSET_ERR_IO;
    }
}

static void chunk_relpath(const uint8_t hash[32], char out[80]) {
    char hex[65]; asset_hash_hex(hash, hex);
    snprintf(out, 80, "chunks/%c%c/%s", hex[0], hex[1], hex);
}

/* Read exactly len bytes of the named chunk into dst and verify. */
static int read_chunk(asset_store *s, const chunk_ref *c, uint8_t *dst) {
    char rel[80]; chunk_relpath(c->hash, rel);
    int fd = asset_store_open_beneath(s, rel);
    if (fd < 0) return map_errno();
    uint64_t t0 = now_ns();
    size_t got = 0;
    while (got < c->len) {
        ssize_t r = pread(fd, dst + got, c->len - got, (off_t)got);
        if (r < 0) { if (errno == EINTR) continue; int e = errno; close(fd); errno = e; return ASSET_ERR_IO; }
        if (r == 0) { close(fd); errno = EIO; return ASSET_ERR_IO; }
        got += (size_t)r;
    }
    /* A file longer than the manifest says is also a mismatch. */
    uint8_t probe;
    ssize_t extra = pread(fd, &probe, 1, (off_t)c->len);
    uint64_t t1 = now_ns();
    close(fd);
    pthread_mutex_lock(&s->mu);
    s->stats.chunks_read++; s->stats.bytes_read += c->len; s->stats.read_ns_total += t1 - t0;
    pthread_mutex_unlock(&s->mu);
    if (extra != 0) return ASSET_ERR_HASH;
    uint8_t h[32]; asset_sha256(dst, c->len, h);
    if (memcmp(h, c->hash, 32) != 0) {
        pthread_mutex_lock(&s->mu); s->stats.hash_failures++; pthread_mutex_unlock(&s->mu);
        return ASSET_ERR_HASH;
    }
    return ASSET_OK;
}

/* ---------------------------------------------------------------- workers */

static int pick_request(asset_store *s, req_t *out) {
    if (s->npending == 0) return 0;
    unsigned best = 0;
    if (s->cfg.plan == ASSET_PLAN_HDD) {
        /* Priority first, then manifest order (asset, mip): the nearest thing
         * to disk order a prototype without extents can offer. */
        for (unsigned i = 1; i < s->npending; ++i) {
            const req_t *a = &s->pending[i], *b = &s->pending[best];
            if (a->priority < b->priority ||
               (a->priority == b->priority && (a->h < b->h || (a->h == b->h && a->mip < b->mip))))
                best = i;
        }
    } else {
        for (unsigned i = 1; i < s->npending; ++i)
            if (s->pending[i].priority < s->pending[best].priority ||
               (s->pending[i].priority == s->pending[best].priority && s->pending[i].seq < s->pending[best].seq))
                best = i;
    }
    *out = s->pending[best];
    s->pending[best] = s->pending[--s->npending];
    return 1;
}

static void push_completion(asset_store *s, completion_t *c) {
    c->next = NULL;
    if (s->done_tail) s->done_tail->next = c; else s->done_head = c;
    s->done_tail = c;
}

static void *reader_main(void *arg) {
    asset_store *s = (asset_store *)arg;
    for (;;) {
        req_t r;
        pthread_mutex_lock(&s->mu);
        while (!s->stopping && s->npending == 0) pthread_cond_wait(&s->cv, &s->mu);
        if (s->stopping) { pthread_mutex_unlock(&s->mu); return NULL; }
        pick_request(s, &r);
        asset_t *a = &s->assets[r.h];
        /* Snapshot what we need; chunk refs are immutable while inflight_count > 0. */
        mip_t *m = &a->mips[r.mip];
        chunk_ref *chunks = a->chunks + m->first_chunk;
        uint32_t nchunks = m->nchunks;
        uint64_t bytes = m->bytes;
        pthread_mutex_unlock(&s->mu);

        completion_t *c = (completion_t *)calloc(1, sizeof *c);
        if (!c) continue;   /* out of memory: the request is lost, poll reports nothing */
        c->h = r.h; c->mip = r.mip; c->len = bytes;
        c->data = (uint8_t *)malloc(bytes ? bytes : 1);
        if (!c->data) { c->rc = ASSET_ERR_NOMEM; }
        else {
            uint64_t off = 0;
            c->rc = ASSET_OK;
            for (uint32_t i = 0; i < nchunks; ++i) {
                int rc = read_chunk(s, &chunks[i], c->data + off);
                if (rc != ASSET_OK) { c->rc = rc; break; }
                off += chunks[i].len;
            }
            if (c->rc != ASSET_OK) { free(c->data); c->data = NULL; }
        }
        pthread_mutex_lock(&s->mu);
        push_completion(s, c);
        pthread_mutex_unlock(&s->mu);
    }
}

/* ------------------------------------------------------------- open/close */

int asset_store_open(const char *root, const asset_store_config *cfg, asset_store **out) {
    if (!root || !out) return ASSET_ERR_ARG;
    asset_store *s = (asset_store *)calloc(1, sizeof *s);
    if (!s) return ASSET_ERR_NOMEM;
    if (cfg) s->cfg = *cfg;
    if (s->cfg.hot_set_bytes == 0) s->cfg.hot_set_bytes = 192u << 20;
    if (s->cfg.max_assets == 0)    s->cfg.max_assets = 256;
    if (s->cfg.max_pending == 0)   s->cfg.max_pending = 1024;
    if (s->cfg.readers == 0)       s->cfg.readers = 4;
    if (s->cfg.plan == ASSET_PLAN_HDD) s->cfg.readers = 1;

    s->dirfd = open(root, O_PATH | O_DIRECTORY | O_CLOEXEC);
    if (s->dirfd < 0) { free(s); return ASSET_ERR_IO; }
    s->assets  = (asset_t *)calloc(s->cfg.max_assets, sizeof(asset_t));
    s->pending = (req_t *)calloc(s->cfg.max_pending, sizeof(req_t));
    s->threads = (pthread_t *)calloc(s->cfg.readers, sizeof(pthread_t));
    if (!s->assets || !s->pending || !s->threads) { asset_store_close(s); return ASSET_ERR_NOMEM; }
    s->cap_pending = s->cfg.max_pending;
    pthread_mutex_init(&s->mu, NULL);
    pthread_cond_init(&s->cv, NULL);
    for (unsigned i = 0; i < s->cfg.readers; ++i) {
        if (pthread_create(&s->threads[i], NULL, reader_main, s) != 0) { asset_store_close(s); return ASSET_ERR_IO; }
        s->nthreads++;
    }
    *out = s;
    return ASSET_OK;
}

static void free_asset(asset_t *a, size_t *resident_bytes) {
    for (uint32_t i = 0; i < a->nmips; ++i) {
        if (a->mips[i].data) { *resident_bytes -= a->mips[i].bytes; free(a->mips[i].data); }
    }
    free(a->chunks);
    memset(a, 0, sizeof *a);
}

void asset_store_close(asset_store *s) {
    if (!s) return;
    if (s->nthreads) {
        pthread_mutex_lock(&s->mu); s->stopping = 1; pthread_cond_broadcast(&s->cv); pthread_mutex_unlock(&s->mu);
        for (unsigned i = 0; i < s->nthreads; ++i) pthread_join(s->threads[i], NULL);
        pthread_mutex_destroy(&s->mu); pthread_cond_destroy(&s->cv);
    }
    for (completion_t *c = s->done_head; c;) { completion_t *n = c->next; free(c->data); free(c); c = n; }
    if (s->assets) for (unsigned i = 0; i < s->cfg.max_assets; ++i) if (s->assets[i].used) free_asset(&s->assets[i], &s->resident_bytes);
    free(s->assets); free(s->pending); free(s->threads);
    if (s->dirfd >= 0) close(s->dirfd);
    free(s);
}

/* -------------------------------------------------------------- manifest */

/* Manifest text format (one canonical spelling, hashed as bytes):
 *   WARDEN-ASSET 1
 *   kind image|blob
 *   image W H STRIDE_PX            (image only)
 *   mips N
 *   mip I NCHUNKS                  (then NCHUNKS lines:)
 *   chunk HEX64 LEN
 * Mip data is the concatenation of its chunks in order. */
static int parse_manifest(asset_t *a, char *text, size_t len) {
    (void)len;
    char *save = NULL;
    char *line = strtok_r(text, "\n", &save);
    if (!line || strcmp(line, "WARDEN-ASSET 1") != 0) return ASSET_ERR_FORMAT;
    line = strtok_r(NULL, "\n", &save);
    if (!line) return ASSET_ERR_FORMAT;
    if (strcmp(line, "kind image") == 0) a->kind = ASSET_KIND_IMAGE;
    else if (strcmp(line, "kind blob") == 0) a->kind = ASSET_KIND_BLOB;
    else return ASSET_ERR_FORMAT;
    if (a->kind == ASSET_KIND_IMAGE) {
        line = strtok_r(NULL, "\n", &save);
        if (!line || sscanf(line, "image %u %u %u", &a->w, &a->h, &a->stride) != 3) return ASSET_ERR_FORMAT;
        if (a->w == 0 || a->h == 0 || a->stride < a->w || a->stride > (1u << 16)) return ASSET_ERR_FORMAT;
    }
    line = strtok_r(NULL, "\n", &save);
    if (!line || sscanf(line, "mips %u", &a->nmips) != 1 || a->nmips == 0 || a->nmips > ASSET_MAX_MIPS) return ASSET_ERR_FORMAT;
    uint32_t cap = 16, n = 0;
    a->chunks = (chunk_ref *)malloc(cap * sizeof(chunk_ref));
    if (!a->chunks) return ASSET_ERR_NOMEM;
    for (uint32_t m = 0; m < a->nmips; ++m) {
        unsigned idx, nch;
        line = strtok_r(NULL, "\n", &save);
        if (!line || sscanf(line, "mip %u %u", &idx, &nch) != 2 || idx != m || nch == 0 || nch > (1u << 20)) return ASSET_ERR_FORMAT;
        a->mips[m].first_chunk = n; a->mips[m].nchunks = nch; a->mips[m].bytes = 0;
        for (unsigned i = 0; i < nch; ++i) {
            char hex[80]; unsigned clen;
            line = strtok_r(NULL, "\n", &save);
            if (!line || sscanf(line, "chunk %79s %u", hex, &clen) != 2) return ASSET_ERR_FORMAT;
            if (clen == 0 || clen > ASSET_CHUNK_BYTES) return ASSET_ERR_FORMAT;
            if (n == cap) {
                cap *= 2;
                chunk_ref *nc = (chunk_ref *)realloc(a->chunks, cap * sizeof(chunk_ref));
                if (!nc) return ASSET_ERR_NOMEM;
                a->chunks = nc;
            }
            if (asset_hash_parse(hex, a->chunks[n].hash) != ASSET_OK) return ASSET_ERR_FORMAT;
            a->chunks[n].len = clen;
            a->mips[m].bytes += clen;
            n++;
        }
        if (a->kind == ASSET_KIND_IMAGE && m == 0 && a->mips[0].bytes != (uint64_t)a->stride * a->h * 4u)
            return ASSET_ERR_FORMAT;   /* mip 0 must be exactly the ARGB8888 surface */
    }
    if (strtok_r(NULL, "\n", &save) != NULL) return ASSET_ERR_FORMAT;   /* trailing junk */
    a->nchunks = n;
    return ASSET_OK;
}

int asset_open(asset_store *s, const uint8_t mh[32], asset_handle *out) {
    if (!s || !mh || !out) return ASSET_ERR_ARG;
    char hex[65]; asset_hash_hex(mh, hex);
    char rel[96]; snprintf(rel, sizeof rel, "manifests/%s", hex);
    int fd = asset_store_open_beneath(s, rel);
    if (fd < 0) return map_errno();
    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size <= 0 || st.st_size > (1 << 24)) { close(fd); return ASSET_ERR_FORMAT; }
    size_t len = (size_t)st.st_size;
    char *text = (char *)malloc(len + 1);
    if (!text) { close(fd); return ASSET_ERR_NOMEM; }
    size_t got = 0;
    while (got < len) {
        ssize_t r = pread(fd, text + got, len - got, (off_t)got);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) { close(fd); free(text); return ASSET_ERR_IO; }
        got += (size_t)r;
    }
    close(fd);
    text[len] = 0;
    uint8_t h[32]; asset_sha256(text, len, h);
    if (memcmp(h, mh, 32) != 0) {
        free(text);
        pthread_mutex_lock(&s->mu); s->stats.hash_failures++; pthread_mutex_unlock(&s->mu);
        return ASSET_ERR_HASH;
    }
    if (memchr(text, 0, len)) { free(text); return ASSET_ERR_FORMAT; }

    pthread_mutex_lock(&s->mu);
    unsigned slot = s->cfg.max_assets;
    for (unsigned i = 0; i < s->cfg.max_assets; ++i) if (!s->assets[i].used) { slot = i; break; }
    if (slot == s->cfg.max_assets) { pthread_mutex_unlock(&s->mu); free(text); return ASSET_ERR_LIMIT; }
    asset_t *a = &s->assets[slot];
    memset(a, 0, sizeof *a);
    a->used = 1;
    memcpy(a->mhash, mh, 32);
    int rc = parse_manifest(a, text, len);
    free(text);
    if (rc != ASSET_OK) { free(a->chunks); memset(a, 0, sizeof *a); pthread_mutex_unlock(&s->mu); return rc; }
    a->generation = 1;
    s->nassets++;
    pthread_mutex_unlock(&s->mu);
    *out = (asset_handle)slot;
    return ASSET_OK;
}

static asset_t *get_asset(asset_store *s, asset_handle h) {
    if (h >= s->cfg.max_assets || !s->assets[h].used || s->assets[h].closing) return NULL;
    return &s->assets[h];
}

int asset_close(asset_store *s, asset_handle h) {
    if (!s) return ASSET_ERR_ARG;
    pthread_mutex_lock(&s->mu);
    asset_t *a = get_asset(s, h);
    if (!a) { pthread_mutex_unlock(&s->mu); return ASSET_ERR_ARG; }
    /* Drop queued requests for it. */
    for (unsigned i = 0; i < s->npending;) {
        if (s->pending[i].h == h) s->pending[i] = s->pending[--s->npending]; else ++i;
    }
    for (uint32_t m = 0; m < a->nmips; ++m) a->mips[m].wanted = 0;
    if (a->inflight_count == 0) { free_asset(a, &s->resident_bytes); s->nassets--; }
    else a->closing = 1;   /* freed in asset_poll when the reader finishes */
    pthread_mutex_unlock(&s->mu);
    return ASSET_OK;
}

/* --------------------------------------------------------------- requests */

int asset_want(asset_store *s, asset_handle h, uint32_t mip, uint8_t priority) {
    if (!s) return ASSET_ERR_ARG;
    pthread_mutex_lock(&s->mu);
    asset_t *a = get_asset(s, h);
    if (!a || mip >= a->nmips) { pthread_mutex_unlock(&s->mu); return ASSET_ERR_ARG; }
    mip_t *m = &a->mips[mip];
    if (m->bytes > s->cfg.hot_set_bytes) { pthread_mutex_unlock(&s->mu); return ASSET_ERR_BUDGET; }
    m->wanted = 1; m->priority = priority;
    if (m->resident || m->inflight) {
        /* Already here or on its way: update the priority of a queued request. */
        for (unsigned i = 0; i < s->npending; ++i)
            if (s->pending[i].h == h && s->pending[i].mip == mip) s->pending[i].priority = priority;
        pthread_mutex_unlock(&s->mu);
        return ASSET_OK;
    }
    if (s->npending == s->cap_pending) { pthread_mutex_unlock(&s->mu); return ASSET_ERR_LIMIT; }
    s->pending[s->npending++] = (req_t){ h, mip, priority, s->seq++ };
    m->inflight = 1; a->inflight_count++;
    pthread_cond_signal(&s->cv);
    pthread_mutex_unlock(&s->mu);
    return ASSET_OK;
}

int asset_drop(asset_store *s, asset_handle h, uint32_t mip) {
    if (!s) return ASSET_ERR_ARG;
    pthread_mutex_lock(&s->mu);
    asset_t *a = get_asset(s, h);
    if (!a || mip >= a->nmips) { pthread_mutex_unlock(&s->mu); return ASSET_ERR_ARG; }
    a->mips[mip].wanted = 0;
    /* A queued, not yet started request is simply removed. */
    for (unsigned i = 0; i < s->npending; ++i)
        if (s->pending[i].h == h && s->pending[i].mip == mip) {
            s->pending[i] = s->pending[--s->npending];
            a->mips[mip].inflight = 0; a->inflight_count--;
            break;
        }
    pthread_mutex_unlock(&s->mu);
    return ASSET_OK;   /* eviction of a resident mip happens in asset_poll */
}

int asset_query(asset_store *s, asset_handle h, asset_status *st) {
    if (!s || !st) return ASSET_ERR_ARG;
    pthread_mutex_lock(&s->mu);
    asset_t *a = get_asset(s, h);
    if (!a) { pthread_mutex_unlock(&s->mu); return ASSET_ERR_ARG; }
    memset(st, 0, sizeof *st);
    st->kind = a->kind; st->width = a->w; st->height = a->h; st->stride_px = a->stride;
    st->mips = a->nmips; st->generation = a->generation;
    for (uint32_t i = 0; i < a->nmips; ++i) {
        st->bytes[i] = a->mips[i].bytes;
        if (a->mips[i].resident) st->resident_mask |= 1u << i;
        if (a->mips[i].wanted)   st->wanted_mask   |= 1u << i;
    }
    pthread_mutex_unlock(&s->mu);
    return ASSET_OK;
}

int asset_map(asset_store *s, asset_handle h, uint32_t mip, const void **data, size_t *len) {
    if (!s || !data || !len) return ASSET_ERR_ARG;
    pthread_mutex_lock(&s->mu);
    asset_t *a = get_asset(s, h);
    if (!a || mip >= a->nmips) { pthread_mutex_unlock(&s->mu); return ASSET_ERR_ARG; }
    if (!a->mips[mip].resident) { pthread_mutex_unlock(&s->mu); *data = NULL; *len = 0; return ASSET_NOT_READY; }
    *data = a->mips[mip].data; *len = (size_t)a->mips[mip].bytes;
    pthread_mutex_unlock(&s->mu);
    return ASSET_OK;
}

/* ------------------------------------------------------------------- poll */

static void emit(asset_event *out, size_t max, size_t *n, asset_event ev) {
    if (*n < max) out[(*n)++] = ev;
}

/* Evict the least important resident mip that is less important than `than`.
 * Returns 1 if something was evicted. Caller holds the lock. */
static int evict_one(asset_store *s, uint8_t than, asset_event *out, size_t max, size_t *n) {
    asset_t *best_a = NULL; uint32_t best_m = 0;
    for (unsigned i = 0; i < s->cfg.max_assets; ++i) {
        asset_t *a = &s->assets[i];
        if (!a->used) continue;
        for (uint32_t m = 0; m < a->nmips; ++m) {
            mip_t *mp = &a->mips[m];
            if (!mp->resident || mp->priority <= than) continue;
            if (!best_a || mp->priority > best_a->mips[best_m].priority ||
               (mp->priority == best_a->mips[best_m].priority && m > best_m)) { best_a = a; best_m = m; }
        }
    }
    if (!best_a) return 0;
    mip_t *mp = &best_a->mips[best_m];
    s->resident_bytes -= mp->bytes; free(mp->data); mp->data = NULL; mp->resident = 0; mp->wanted = 0;
    best_a->generation++; s->stats.evictions++;
    emit(out, max, n, (asset_event){ ASSET_EV_EVICTED, (asset_handle)(best_a - s->assets), best_m, best_a->generation, 0 });
    return 1;
}

size_t asset_poll(asset_store *s, asset_event *out, size_t max) {
    size_t n = 0;
    if (!s) return 0;
    pthread_mutex_lock(&s->mu);

    /* 1. Dropped-but-resident mips are evicted here, never under a frame. */
    for (unsigned i = 0; i < s->cfg.max_assets; ++i) {
        asset_t *a = &s->assets[i];
        if (!a->used) continue;
        for (uint32_t m = 0; m < a->nmips; ++m) {
            mip_t *mp = &a->mips[m];
            if (mp->resident && !mp->wanted) {
                s->resident_bytes -= mp->bytes; free(mp->data); mp->data = NULL; mp->resident = 0;
                a->generation++; s->stats.evictions++;
                emit(out, max, &n, (asset_event){ ASSET_EV_EVICTED, (asset_handle)i, m, a->generation, 0 });
            }
        }
    }

    /* 2. Install completed reads. */
    completion_t *c = s->done_head;
    s->done_head = s->done_tail = NULL;
    while (c) {
        completion_t *next = c->next;
        asset_t *a = &s->assets[c->h];
        mip_t *mp = &a->mips[c->mip];
        a->inflight_count--;
        mp->inflight = 0;
        if (a->closing) {
            free(c->data);
            if (a->inflight_count == 0) { free_asset(a, &s->resident_bytes); s->nassets--; }
        } else if (c->rc != ASSET_OK) {
            mp->wanted = 0;
            emit(out, max, &n, (asset_event){ ASSET_EV_FAILED, c->h, c->mip, a->generation, c->rc });
        } else if (!mp->wanted) {
            free(c->data);   /* dropped while in flight */
        } else {
            int fits = 1;
            while (s->resident_bytes + c->len > s->cfg.hot_set_bytes) {
                if (!evict_one(s, mp->priority, out, max, &n)) { fits = 0; break; }
            }
            if (!fits) {
                free(c->data); mp->wanted = 0;
                emit(out, max, &n, (asset_event){ ASSET_EV_FAILED, c->h, c->mip, a->generation, ASSET_ERR_BUDGET });
            } else {
                if (mp->resident) { s->resident_bytes -= mp->bytes; free(mp->data); }
                mp->data = c->data; mp->resident = 1; s->resident_bytes += c->len;
                a->generation++;
                emit(out, max, &n, (asset_event){ ASSET_EV_RESIDENT, c->h, c->mip, a->generation, 0 });
            }
        }
        free(c);
        c = next;
    }
    pthread_mutex_unlock(&s->mu);
    return n;
}

void asset_store_stats_get(asset_store *s, asset_store_stats *out) {
    if (!s || !out) return;
    pthread_mutex_lock(&s->mu); *out = s->stats; pthread_mutex_unlock(&s->mu);
}
