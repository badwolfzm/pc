/*
 * assetpack — build a content-addressed store from files.
 *
 *   assetpack <store-root> blob  <file>                       -> prints manifest hash
 *   assetpack <store-root> image <raw-argb8888> <w> <h> [stride_px] [--tiles]
 *
 * Writes chunks/XX/<sha256> (64 KiB chunks, one copy per distinct chunk) and
 * manifests/<sha256-of-manifest>. The image form generates a box-filtered
 * mip chain down to 1x1 and lists mips smallest-last? No: mip 0 is the full
 * surface, mips 1..n are the halves. The reader treats "mip tail" as the
 * highest-numbered mips. With --tiles, mip 0 is reordered into 16x16 ARGB
 * tiles, row-major, 1 KiB each, for the canvas thread's background restore.
 *
 * No compression in this prototype: the design calls for LZ4, this writes raw.
 */
#define _GNU_SOURCE
#include "asset_store.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int write_whole(const char *path, const void *data, size_t len) {
    char tmp[4096]; snprintf(tmp, sizeof tmp, "%s.tmp", path);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0444);
    if (fd < 0) return -1;
    size_t off = 0;
    while (off < len) { ssize_t w = write(fd, (const char *)data + off, len - off); if (w <= 0) { close(fd); return -1; } off += (size_t)w; }
    if (fsync(fd) != 0 || close(fd) != 0) return -1;
    return rename(tmp, path);   /* a chunk appears whole or not at all */
}

static int store_chunk(const char *root, const uint8_t *p, size_t len, char hex_out[65]) {
    uint8_t h[32]; asset_sha256(p, len, h); asset_hash_hex(h, hex_out);
    char dir[4096], path[4096];
    snprintf(dir, sizeof dir, "%s/chunks/%c%c", root, hex_out[0], hex_out[1]);
    mkdir(dir, 0755);
    snprintf(path, sizeof path, "%s/%s", dir, hex_out);
    struct stat st;
    if (stat(path, &st) == 0 && (size_t)st.st_size == len) return 0;   /* dedup: already there */
    return write_whole(path, p, len);
}

/* Append chunk lines for one level's bytes; returns chunk count. */
static int emit_level(FILE *mf, const char *root, const uint8_t *p, size_t len, unsigned level) {
    unsigned n = (unsigned)((len + ASSET_CHUNK_BYTES - 1) / ASSET_CHUNK_BYTES);
    fprintf(mf, "mip %u %u\n", level, n);
    for (size_t off = 0; off < len; off += ASSET_CHUNK_BYTES) {
        size_t cl = (len - off < ASSET_CHUNK_BYTES) ? len - off : ASSET_CHUNK_BYTES;
        char hex[65];
        if (store_chunk(root, p + off, cl, hex) != 0) { perror("store_chunk"); return -1; }
        fprintf(mf, "chunk %s %zu\n", hex, cl);
    }
    return (int)n;
}

static uint8_t *read_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return NULL; }
    uint8_t *b = (uint8_t *)malloc((size_t)sz + 1);
    if (!b || fread(b, 1, (size_t)sz, f) != (size_t)sz) { fclose(f); free(b); return NULL; }
    fclose(f); *len = (size_t)sz; return b;
}

/* Box filter one ARGB8888 level to half size. */
static uint8_t *downsample(const uint8_t *src, unsigned w, unsigned h, unsigned stride_px, unsigned *ow, unsigned *oh) {
    unsigned nw = w > 1 ? w / 2 : 1, nh = h > 1 ? h / 2 : 1;
    uint8_t *dst = (uint8_t *)malloc((size_t)nw * nh * 4);
    if (!dst) return NULL;
    for (unsigned y = 0; y < nh; ++y) for (unsigned x = 0; x < nw; ++x) {
        unsigned sx = x * 2, sy = y * 2;
        unsigned x1 = sx + 1 < w ? sx + 1 : sx, y1 = sy + 1 < h ? sy + 1 : sy;
        for (unsigned c = 0; c < 4; ++c) {
            unsigned a = src[((size_t)sy * stride_px + sx) * 4 + c], b = src[((size_t)sy * stride_px + x1) * 4 + c];
            unsigned cc = src[((size_t)y1 * stride_px + sx) * 4 + c], d = src[((size_t)y1 * stride_px + x1) * 4 + c];
            dst[((size_t)y * nw + x) * 4 + c] = (uint8_t)((a + b + cc + d + 2) / 4);
        }
    }
    *ow = nw; *oh = nh;
    return dst;
}

/* Reorder a W x H ARGB surface into 16x16 tiles, row-major; W and H must be multiples of 16. */
static uint8_t *tile16(const uint8_t *src, unsigned w, unsigned h, unsigned stride_px) {
    uint8_t *dst = (uint8_t *)malloc((size_t)w * h * 4);
    if (!dst) return NULL;
    size_t o = 0;
    for (unsigned ty = 0; ty < h; ty += 16) for (unsigned tx = 0; tx < w; tx += 16)
        for (unsigned y = 0; y < 16; ++y) { memcpy(dst + o, src + ((size_t)(ty + y) * stride_px + tx) * 4, 64); o += 64; }
    return dst;
}

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: assetpack <root> blob <file> | image <raw-argb> <w> <h> [stride_px] [--tiles]\n"); return 2; }
    const char *root = argv[1];
    if (strlen(root) > 3000) { fprintf(stderr, "store root path too long\n"); return 2; }
    char p[4096];
    snprintf(p, sizeof p, "%s", root); mkdir(p, 0755);
    snprintf(p, sizeof p, "%s/chunks", root); mkdir(p, 0755);
    snprintf(p, sizeof p, "%s/manifests", root); mkdir(p, 0755);

    char *mtext = NULL; size_t mlen = 0;
    FILE *mf = open_memstream(&mtext, &mlen);
    if (!mf) { perror("open_memstream"); return 1; }
    fprintf(mf, "WARDEN-ASSET 1\n");

    if (strcmp(argv[2], "blob") == 0) {
        size_t len; uint8_t *b = read_file(argv[3], &len);
        if (!b || len == 0) { fprintf(stderr, "cannot read %s\n", argv[3]); return 1; }
        fprintf(mf, "kind blob\nmips 1\n");
        if (emit_level(mf, root, b, len, 0) < 0) return 1;
        free(b);
    } else if (strcmp(argv[2], "image") == 0) {
        if (argc < 6) { fprintf(stderr, "image needs <raw-argb> <w> <h>\n"); return 2; }
        unsigned w = (unsigned)atoi(argv[4]), h = (unsigned)atoi(argv[5]);
        unsigned stride = (argc > 6 && argv[6][0] != '-') ? (unsigned)atoi(argv[6]) : w;
        int tiles = 0;
        for (int i = 6; i < argc; ++i) if (strcmp(argv[i], "--tiles") == 0) tiles = 1;
        if (w == 0 || h == 0 || stride < w) { fprintf(stderr, "bad geometry\n"); return 2; }
        if (tiles && (w % 16 || h % 16)) { fprintf(stderr, "--tiles needs w and h multiples of 16\n"); return 2; }
        size_t len; uint8_t *b = read_file(argv[3], &len);
        if (!b || len < (size_t)stride * h * 4) { fprintf(stderr, "raw image too short for %ux%u stride %u\n", w, h, stride); return 1; }
        /* Count levels. */
        unsigned levels = 1; for (unsigned a = w, c = h; a > 1 || c > 1; a = a > 1 ? a / 2 : 1, c = c > 1 ? c / 2 : 1) levels++;
        if (levels > ASSET_MAX_MIPS) levels = ASSET_MAX_MIPS;
        fprintf(mf, "kind image\nimage %u %u %u\nmips %u\n", w, h, tiles ? w : stride, levels);
        /* Mip 0: as stored (stride preserved) or tiled (stride == w). */
        uint8_t *l0 = tiles ? tile16(b, w, h, stride) : b;
        if (emit_level(mf, root, l0, (size_t)(tiles ? w : stride) * h * 4, 0) < 0) return 1;
        if (tiles) free(l0);
        const uint8_t *cur = b; unsigned cw = w, ch = h, cs = stride; uint8_t *owned = NULL;
        for (unsigned lv = 1; lv < levels; ++lv) {
            unsigned nw, nh; uint8_t *nx = downsample(cur, cw, ch, cs, &nw, &nh);
            if (!nx) return 1;
            free(owned); owned = nx; cur = nx; cw = nw; ch = nh; cs = nw;
            if (emit_level(mf, root, cur, (size_t)cw * ch * 4, lv) < 0) return 1;
        }
        free(owned); free(b);
    } else { fprintf(stderr, "unknown kind %s\n", argv[2]); return 2; }

    fclose(mf);
    uint8_t h[32]; char hex[65];
    asset_sha256(mtext, mlen, h); asset_hash_hex(h, hex);
    snprintf(p, sizeof p, "%s/manifests/%s", root, hex);
    if (write_whole(p, mtext, mlen) != 0) { perror("write manifest"); return 1; }
    printf("%s\n", hex);
    free(mtext);
    return 0;
}
