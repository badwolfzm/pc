/* game_demo.c - one sandboxed game (a child process) draws through the ring into the
 * render service, which streams a 720p background and a sprite from a hash-verified
 * asset store and presents changed tiles to a sink that writes PPM snapshots.
 * usage: game_demo <scratch-dir> [frames] [fps-cap]   (needs ./assetpack next to the binary)
 * The child holds only the ring mapping. It never sees the store, the surfaces or the
 * canvas. On the kernel this child is the app in its own address space; here it is a
 * fork()ed process with every fd but stdio closed. */
#define _GNU_SOURCE
#include "mx_render.h"
#include "mx_rcs_build.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/prctl.h>

#define W 1280
#define H 720
#define NSPR 12
static int run(const char *cmd) { int rc = system(cmd); return WIFEXITED(rc) ? WEXITSTATUS(rc) : -1; }
static void nap(void) { struct timespec ts = { 0, 100000 }; nanosleep(&ts, NULL); }
static int read_hash(const char *path, uint8_t out[32]) { char hex[80] = { 0 }; FILE *f = fopen(path, "r"); if (!f) return -1; int ok = fscanf(f, "%79s", hex) == 1; fclose(f); return ok && asset_hash_parse(hex, out) == ASSET_OK ? 0 : -1; }

/* ---- sink: count, hash, and snapshot the ghost to PPM at chosen frames ---- */
typedef struct { const char *dir; uint64_t tiles, bytes, hash; uint32_t frames; mx_render_service *svc; } ppm_sink;
static int ps_tile(mx_render_sink *k, uint32_t ci, uint32_t f, uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint32_t *px, uint32_t stride) {
    (void)ci; (void)f; (void)x; (void)y; ppm_sink *s = k->user;
    for (uint32_t j = 0; j < h; ++j) for (uint32_t i = 0; i < w; ++i) s->hash = (s->hash ^ px[j * stride + i]) * 1099511628211ull;
    s->tiles++; s->bytes += (uint64_t)w * h * 4; return 0;
}
static void ps_done(mx_render_sink *k, uint32_t ci, uint32_t f, const gc_stats *st, uint32_t w, uint32_t h) {
    (void)st; ppm_sink *s = k->user; s->frames++;
    if (f != 1 && f != 30 && f % 60 != 0) return;
    char path[4096]; snprintf(path, sizeof path, "%s/frame_%04u.ppm", s->dir, f);
    FILE *o = fopen(path, "wb"); if (!o) return;
    fprintf(o, "P6\n%u %u\n255\n", w, h);
    const uint32_t *g = s->svc->clients[ci].canvas.ghost;
    for (uint32_t i = 0; i < w * h; ++i) { uint8_t rgb[3] = { (uint8_t)(g[i] >> 16), (uint8_t)(g[i] >> 8), (uint8_t)g[i] }; fwrite(rgb, 1, 3, o); }
    fclose(o);
}

/* ---- the game (child) ---- */
typedef struct { int x, y, vx, vy; } spr;
static uint32_t wait_result(mx_ring *r, rcs_result *res) { for (;;) { if (mx_ring_get_result(r, res)) return res->status; if (atomic_load(&r->revoked)) return 99; nap(); } }

static int game(mx_ring *r, int frames, int fps_cap) {
    /* 1. wait for policy's assets: slot 0 background, slot 1 sprite */
    for (int i = 0; i < 200000 && !(mx_ring_asset_ready(r, 0) && mx_ring_asset_ready(r, 1)); ++i) nap();
    if (!(mx_ring_asset_ready(r, 0) && mx_ring_asset_ready(r, 1))) { fprintf(stderr, "game: assets never became ready\n"); return 3; }
    rcs_result res;
    /* 2. resources: 0 display, 1 background, 2 sprite; bind the two assets */
    uint8_t *p = mx_ring_reserve(r, 4096); mxb b; mxb_begin(&b, p, 4096, RCS_BATCH_RESOURCE);
    mxb_create_surface(&b, 0, W, H, RCS_FMT_ARGB8888); mxb_create_surface(&b, 1, W, H, RCS_FMT_ARGB8888); mxb_create_surface(&b, 2, 64, 64, RCS_FMT_ARGB8888);
    mxb_bind_asset(&b, 0, 1); mxb_bind_asset(&b, 1, 2);
    mx_ring_publish(r, p, mxb_end(&b));
    if (wait_result(r, &res) != RCS_OK) { fprintf(stderr, "game: resources rejected: %s\n", rcs_error_name(res.error)); return 4; }
    /* 3. frames */
    spr s[NSPR], prev[NSPR];
    for (int i = 0; i < NSPR; ++i) { s[i] = (spr){ 100 + i * 90, 80 + (i * 53) % 500, 2 + i % 4, 1 + i % 3 }; prev[i] = s[i]; }
    uint64_t t_start = mx_now_ns(), frame_ns = fps_cap > 0 ? 1000000000ull / (uint64_t)fps_cap : 0;
    int rejected = 0;
    for (int f = 1; f <= frames; ++f) {
        while (!(p = mx_ring_reserve(r, 8192))) nap();
        mxb_begin(&b, p, 8192, RCS_BATCH_FRAME);
        mxb_begin_frame(&b, (uint32_t)f);
        if (f == 1) mxb_blit(&b, 0, 0, 0, 1, (rcs_rect){ 0, 0, W, H }, RCS_BLEND_COPY);
        else for (int i = 0; i < NSPR; ++i) mxb_blit(&b, 0, prev[i].x, prev[i].y, 1, (rcs_rect){ prev[i].x, prev[i].y, 64, 64 }, RCS_BLEND_COPY);
        for (int i = 0; i < NSPR; ++i) mxb_blit(&b, 0, s[i].x, s[i].y, 2, (rcs_rect){ 0, 0, 64, 64 }, RCS_BLEND_ALPHA);
        mxb_fill_rect(&b, 0, (rcs_rect){ 16, 16, 240, 20 }, 0xFF202020u);
        mxb_fill_rect(&b, 0, (rcs_rect){ 18, 18, 1 + (int32_t)(235 * (f % 100) / 100), 16 }, 0xFF40C040u);   /* a moving bar: the HUD */
        mxb_line(&b, 0, 0, H - 1, (int32_t)((f * 9) % W), 0, 0xFFFFFFFFu);
        mxb_present(&b, 0, (uint32_t)f);
        mxb_fence(&b, (uint32_t)f);
        mx_ring_publish(r, p, mxb_end(&b));
        if (wait_result(r, &res) != RCS_OK) { rejected++; fprintf(stderr, "game: frame %d rejected: %s at %u\n", f, rcs_error_name(res.error), res.failing_index); }
        for (int i = 0; i < NSPR; ++i) { prev[i] = s[i]; s[i].x += s[i].vx; s[i].y += s[i].vy; if (s[i].x < 0 || s[i].x > W - 64) { s[i].vx = -s[i].vx; s[i].x += 2 * s[i].vx; } if (s[i].y < 0 || s[i].y > H - 64) { s[i].vy = -s[i].vy; s[i].y += 2 * s[i].vy; } }
        if (frame_ns) { uint64_t due = t_start + (uint64_t)f * frame_ns; while (mx_now_ns() < due) nap(); }
    }
    return rejected ? 5 : 0;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <scratch-dir> [frames] [fps-cap]\n", argv[0]); return 2; }
    const char *dir = argv[1]; int frames = argc > 2 ? atoi(argv[2]) : 120; int fps_cap = argc > 3 ? atoi(argv[3]) : 0;
    char cmd[8192], path[4096];
    snprintf(cmd, sizeof cmd, "rm -rf '%s' && mkdir -p '%s/store'", dir, dir); if (run(cmd) != 0) return 2;
    /* pack a 720p background and a 64x64 alpha sprite */
    snprintf(path, sizeof path, "%s/bg.raw", dir);
    { FILE *f = fopen(path, "wb"); for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) { uint32_t sky = (uint32_t)(40 + y * 120 / H); uint32_t v = (x / 64 + y / 64) & 1 ? 0xFF000000u | (sky << 16) | (sky << 8) | 0x80u : 0xFF000000u | ((sky / 2) << 16) | ((sky + 40) << 8) | 0x60u; if (y > 600 && ((x * 7 + y * 3) % 97) < 5) v = 0xFFE0E0A0u; fwrite(&v, 4, 1, f); } fclose(f); }
    snprintf(path, sizeof path, "%s/sprite.raw", dir);
    { FILE *f = fopen(path, "wb"); for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x) { int dx = x - 32, dy = y - 32, d = dx * dx + dy * dy; uint32_t a = d < 784 ? 255u : d < 1024 ? 96u : 0u; uint32_t v = (a << 24) | ((uint32_t)(255 - d / 5 > 0 ? 255 - d / 5 : 0) << 16) | ((uint32_t)(x * 3) << 8) | (uint32_t)(y * 3); fwrite(&v, 4, 1, f); } fclose(f); }
    snprintf(cmd, sizeof cmd, "./assetpack '%s/store' image '%s/bg.raw' %d %d > '%s/bg.hash' && ./assetpack '%s/store' image '%s/sprite.raw' 64 64 > '%s/sprite.hash'", dir, dir, W, H, dir, dir, dir, dir);
    if (run(cmd) != 0) { fprintf(stderr, "assetpack failed\n"); return 2; }
    uint8_t bg_h[32], sp_h[32];
    snprintf(path, sizeof path, "%s/bg.hash", dir); if (read_hash(path, bg_h)) return 2;
    snprintf(path, sizeof path, "%s/sprite.hash", dir); if (read_hash(path, sp_h)) return 2;

    /* the host runtime: store, service, one ring, one client */
    asset_store_config cfg = { .hot_set_bytes = 64u << 20, .plan = ASSET_PLAN_SSD, .readers = 2 };
    asset_store *store = NULL; snprintf(path, sizeof path, "%s/store", dir);
    if (asset_store_open(path, &cfg, &store) != ASSET_OK) { fprintf(stderr, "store open failed\n"); return 2; }
    mx_render_service svc; ppm_sink ps = { dir, 0, 0, 1469598103934665603ull, 0, &svc }; mx_render_sink sink = { ps_tile, ps_done, &ps };
    mx_render_init(&svc, store, &sink);
    mx_ring *ring = mmap(NULL, sizeof *ring, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    mx_ring_init(ring, sizeof *ring);
    int ci = mx_render_attach(&svc, ring, 0x600D, RCS_R_ALL);
    /* policy: the game's manifest names two assets; grant them by slot. The game never sees a hash. */
    if (mx_render_grant_image(&svc, ci, 0, bg_h, 0, 0) != ASSET_OK || mx_render_grant_image(&svc, ci, 1, sp_h, 0, 0) != ASSET_OK) { fprintf(stderr, "grant failed\n"); return 2; }

    uint64_t t_fork = mx_now_ns();
    pid_t pid = fork();
    if (pid == 0) {
        /* the sandboxed side: nothing but the ring mapping and stdio */
        asset_store_close(store);            /* child copies of the reader threads' fds: drop them */
        for (int fd = 3; fd < 1024; ++fd) close(fd);
        prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
        _exit(game(ring, frames, fps_cap));
    }
    int status = 0; uint64_t t_first = 0, pumps = 0, busy = 0;
    for (;;) {
        uint64_t p0 = mx_now_ns(); uint32_t n = mx_render_pump(&svc); pumps++;
        if (n) { busy += mx_now_ns() - p0; if (!t_first && svc.clients[ci].stats.presents) t_first = mx_now_ns(); }
        else { pid_t w = waitpid(pid, &status, WNOHANG); if (w == pid) break; nap(); }
        if (mx_now_ns() - t_fork > 120000000000ull) { kill(pid, 9); waitpid(pid, &status, 0); fprintf(stderr, "timeout\n"); break; }
    }
    mx_render_pump(&svc);
    uint64_t t_end = mx_now_ns();
    mx_render_client_stats st = svc.clients[ci].stats;
    printf("game exit %d; frames presented %llu, batches %llu (rejected %llu, bad descriptors %llu)\n", WIFEXITED(status) ? WEXITSTATUS(status) : -1,
           (unsigned long long)st.presents, (unsigned long long)st.batches, (unsigned long long)st.rejected, (unsigned long long)st.bad_descs);
    printf("assets ready after %.1f ms (fork -> first present incl. load)\n", (double)(t_first - t_fork) / 1e6);
    double secs = (double)(t_end - t_first) / 1e9;
    printf("wall: %.2f s for %llu frames = %.1f fps%s; service busy %.1f ms total = %.3f ms per frame (copy %.3f, submit %.3f, composite %.3f)\n",
           secs, (unsigned long long)st.presents, (double)st.presents / secs, fps_cap ? " (capped)" : " (uncapped: limited by the client's round trip)",
           (double)busy / 1e6, (double)busy / 1e6 / (double)st.presents, (double)st.ns_copy / 1e6 / (double)st.presents, (double)st.ns_submit / 1e6 / (double)st.presents, (double)st.ns_composite / 1e6 / (double)st.presents);
    printf("tiles to the encoder: %llu (%.1f per frame, %.1f KiB per frame) of %u per full frame; consumed hash %016llx\n", (unsigned long long)ps.tiles, (double)ps.tiles / (double)st.presents,
           (double)ps.bytes / 1024.0 / (double)st.presents, (unsigned)((W / 16) * (H / 16)), (unsigned long long)(ps.hash ^ mx_render_display_hash(&svc, ci)));
    printf("snapshots: %s/frame_0001.ppm, frame_0030.ppm, frame_0060.ppm ...\n", dir);
    mx_render_free(&svc); asset_store_close(store); munmap(ring, sizeof *ring);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : 1;
}
