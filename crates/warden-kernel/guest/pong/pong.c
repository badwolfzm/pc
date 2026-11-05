/* pong.c - Pong as a native Warden guest app.
 *
 * Game loop, physics and controls here; rendering through the MatrixOS ghost
 * canvas (matrixos/canvas/ghost_canvas.c, compiled freestanding): draws mark
 * 16x16 tiles, gc_present() hands only the tiles that changed to the display
 * syscall. Keyboard through SYS_KEY (PS/2 scancodes), time through the
 * partition timer ticks and rdtsc.
 *
 * Controls: W / S (or up / down arrow) move the left paddle; the right paddle
 * is a simple follower. Q quits. The app runs two timed phases and reports
 * frame counts so the kernel log carries the numbers:
 *   A: gameplay paced to 30 fps for 3 s (the game as played), B: gameplay
 *   uncapped for 2 s (frame-rate ceiling with small damage), C: full-screen
 *   redraw uncapped for 2 s (every tile through the present syscall).
 */
#include "rt.h"
#include "ghost_canvas.h"

#define COURT_BG   0xFF101828u
#define LINE_COL   0xFF405070u
#define PADDLE_COL 0xFFE0E0E0u
#define BALL_COL   0xFFFFD040u
#define SCORE_COL  0xFF80C0FFu
#define PAD_W 6
#define PAD_H 36
#define BALL  6

static gc_canvas canvas;
static uint32_t W, H;
static int lpad_y, rpad_y, ball_x, ball_y, ball_vx, ball_vy;
static int lscore, rscore;
static int lpad_prev, rpad_prev, ball_px, ball_py;
static int key_up, key_down;
static uint64_t frames, presents, tiles_sent;

static int sink(void *user, uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint32_t *px, uint32_t stride) {
    (void)user; (void)stride;
    /* px points into the ghost at the tile origin; the kernel wants the buffer base plus the rect. */
    const uint32_t *base = px - ((size_t)y * W + x);
    uint64_t rect = (uint64_t)x | ((uint64_t)y << 16) | ((uint64_t)w << 32) | ((uint64_t)h << 48);
    uint64_t r = sys3(SYS_PRESENT, SLOT_DISPLAY, (uint64_t)(uintptr_t)base, rect);
    tiles_sent++;
    return r == 0 ? 0 : (int)r;
}

/* 3x5 block digits for the score. */
static const uint8_t DIG[10][5] = {
    {7,5,5,5,7},{2,6,2,2,7},{7,1,7,4,7},{7,1,7,1,7},{5,5,7,1,1},{7,4,7,1,7},{7,4,7,5,7},{7,1,1,1,1},{7,5,7,5,7},{7,5,7,1,7}
};
static void draw_digit(int x, int y, int d, int s) {
    for (int r = 0; r < 5; ++r) for (int c = 0; c < 3; ++c)
        gc_rect(&canvas, x + c * s, y + r * s, s, s, (DIG[d][r] >> (2 - c)) & 1 ? SCORE_COL : COURT_BG);
}
static void draw_score(void) {
    draw_digit((int)W / 2 - 40, 8, (lscore / 10) % 10, 4); draw_digit((int)W / 2 - 24, 8, lscore % 10, 4);
    draw_digit((int)W / 2 + 12, 8, (rscore / 10) % 10, 4); draw_digit((int)W / 2 + 28, 8, rscore % 10, 4);
}
static void draw_court(void) {
    gc_clear(&canvas, COURT_BG);
    for (uint32_t y = 0; y < H; y += 12) gc_rect(&canvas, (int)W / 2 - 1, (int)y, 2, 6, LINE_COL);
    draw_score();
}
static void reset_ball(int dir) { ball_x = (int)W / 2; ball_y = (int)H / 2; ball_vx = 2 * dir; ball_vy = (int)(rdtsc() % 3) - 1; if (!ball_vy) ball_vy = 1; }

static void step(void) {
    if (key_up)   lpad_y -= 3;
    if (key_down) lpad_y += 3;
    if (lpad_y < 0) lpad_y = 0;
    if (lpad_y > (int)H - PAD_H) lpad_y = (int)H - PAD_H;
    /* right paddle follows the ball with a lag */
    int target = ball_y - PAD_H / 2;
    if (rpad_y < target - 2) rpad_y += 2; else if (rpad_y > target + 2) rpad_y -= 2;
    if (rpad_y < 0) rpad_y = 0;
    if (rpad_y > (int)H - PAD_H) rpad_y = (int)H - PAD_H;
    ball_x += ball_vx; ball_y += ball_vy;
    if (ball_y <= 0) { ball_y = 0; ball_vy = -ball_vy; }
    if (ball_y >= (int)H - BALL) { ball_y = (int)H - BALL; ball_vy = -ball_vy; }
    if (ball_vx < 0 && ball_x <= 10 + PAD_W && ball_y + BALL >= lpad_y && ball_y <= lpad_y + PAD_H) { ball_x = 10 + PAD_W; ball_vx = -ball_vx; ball_vy += (ball_y + BALL / 2 - (lpad_y + PAD_H / 2)) / 8; }
    if (ball_vx > 0 && ball_x + BALL >= (int)W - 10 - PAD_W && ball_y + BALL >= rpad_y && ball_y <= rpad_y + PAD_H) { ball_x = (int)W - 10 - PAD_W - BALL; ball_vx = -ball_vx; ball_vy += (ball_y + BALL / 2 - (rpad_y + PAD_H / 2)) / 8; }
    if (ball_vy > 4) ball_vy = 4;
    if (ball_vy < -4) ball_vy = -4;
    if (ball_x < 0) { rscore = (rscore + 1) % 100; reset_ball(1); draw_score(); }
    if (ball_x > (int)W) { lscore = (lscore + 1) % 100; reset_ball(-1); draw_score(); }
}

static void draw(void) {
    /* erase old, draw new: only the touched tiles become dirty */
    gc_rect(&canvas, 10, lpad_prev, PAD_W, PAD_H, COURT_BG);
    gc_rect(&canvas, (int)W - 10 - PAD_W, rpad_prev, PAD_W, PAD_H, COURT_BG);
    gc_rect(&canvas, ball_px, ball_py, BALL, BALL, COURT_BG);
    /* the centre line may have been erased by the ball */
    for (uint32_t y = 0; y < H; y += 12) if ((int)y + 6 > ball_py - 2 && (int)y < ball_py + BALL + 2) gc_rect(&canvas, (int)W / 2 - 1, (int)y, 2, 6, LINE_COL);
    gc_rect(&canvas, 10, lpad_y, PAD_W, PAD_H, PADDLE_COL);
    gc_rect(&canvas, (int)W - 10 - PAD_W, rpad_y, PAD_W, PAD_H, PADDLE_COL);
    gc_rect(&canvas, ball_x, ball_y, BALL, BALL, BALL_COL);
    lpad_prev = lpad_y; rpad_prev = rpad_y; ball_px = ball_x; ball_py = ball_y;
}

/* PS/2 set 1: W 0x11, S 0x1F, up 0xE0 0x48, down 0xE0 0x50, Q 0x10; release = code | 0x80 */
static int poll_keys(void) {
    int quit = 0, ext = 0;
    for (int n = 0; n < 32; ++n) {
        uint64_t sc = sys3(SYS_KEY, SLOT_DISPLAY, 0, 0);
        if (!sc) break;
        if (sc == 0xE0) { ext = 1; continue; }
        int make = !(sc & 0x80); uint64_t code = sc & 0x7F;
        if (make) { uint64_t t = sys3(SYS_TICKS, SLOT_DISPLAY, 0, 0); sys3(SYS_REPORT, SLOT_CONSOLE, 100, code | (t << 16) | ((uint64_t)frames << 40)); }
        if (code == 0x11 || (ext && code == 0x48)) key_up = make;
        if (code == 0x1F || (ext && code == 0x50)) key_down = make;
        if (code == 0x10 && make) quit = 1;
        ext = 0;
    }
    return quit;
}

static uint64_t ticks(void) { return sys3(SYS_TICKS, SLOT_DISPLAY, 0, 0); }

static void frame(void) {
    step(); draw();
    if (gc_present(&canvas, sink, 0) != 0) sys3(SYS_EXIT, 2, 0, 0);
    frames++; presents++;
    sys3(SYS_YIELD, 0, 0, 0);   /* refill the partition budget; a frame never spends it */
}

int pong_main(void) {
    uint64_t info = sys3(SYS_DISPLAY_INFO, SLOT_DISPLAY, 0, 0);
    if ((int64_t)info < 0) return 3;
    W = (uint32_t)(info & 0xFFFF); H = (uint32_t)((info >> 16) & 0xFFFF);
    if (gc_init(&canvas, W, H) != 0) return 4;
    lpad_y = rpad_y = (int)H / 2 - PAD_H / 2; lpad_prev = rpad_prev = lpad_y;
    reset_ball(1); ball_px = ball_x; ball_py = ball_y;
    draw_court();
    sys3(SYS_REPORT, SLOT_CONSOLE, 1, ((uint64_t)W << 16) | H);     /* 1: display geometry */
    /* calibrate rdtsc against the 100 Hz partition timer over 20 ticks */
    uint64_t t0 = ticks(); while (ticks() == t0) {} uint64_t c0 = rdtsc(), t1 = ticks();
    while (ticks() < t1 + 20) {} uint64_t tsc_per_tick = (rdtsc() - c0) / 20;
    sys3(SYS_REPORT, SLOT_CONSOLE, 2, tsc_per_tick);                /* 2: TSC cycles per 10 ms tick */
    int quit = 0;
    /* phase A: gameplay paced to 30 fps, 300 ticks (3 s) */
    uint64_t period = tsc_per_tick * 100 / 30, next = rdtsc() + period, late = 0, worst = 0;
    uint64_t start = ticks(), f0 = frames, tl0 = tiles_sent, c_start = rdtsc();
    while (ticks() - start < 300 && !quit) {
        uint64_t a = rdtsc(); quit = poll_keys(); frame(); uint64_t d = rdtsc() - a; if (d > worst) worst = d;
        if (rdtsc() > next + period) late++;
        while (rdtsc() < next) { sys3(SYS_YIELD, 0, 0, 0); }
        next += period;
    }
    uint64_t el = rdtsc() - c_start, n = frames - f0 ? frames - f0 : 1;
    sys3(SYS_REPORT, SLOT_CONSOLE, 10, frames - f0);                                   /* frames */
    sys3(SYS_REPORT, SLOT_CONSOLE, 11, (frames - f0) * 100 * tsc_per_tick * 100 / (el ? el : 1)); /* fps x100 */
    sys3(SYS_REPORT, SLOT_CONSOLE, 12, (tiles_sent - tl0) / n);                        /* tiles per frame */
    sys3(SYS_REPORT, SLOT_CONSOLE, 13, worst * 1000000 / (tsc_per_tick * 100));        /* worst frame, us */
    sys3(SYS_REPORT, SLOT_CONSOLE, 14, late);                                          /* frames that missed their slot */
    /* phase B: gameplay uncapped, 200 ticks */
    worst = 0; start = ticks(); f0 = frames; tl0 = tiles_sent; c_start = rdtsc();
    while (ticks() - start < 200 && !quit) { uint64_t a = rdtsc(); quit = poll_keys(); frame(); uint64_t d = rdtsc() - a; if (d > worst) worst = d; }
    el = rdtsc() - c_start; n = frames - f0 ? frames - f0 : 1;
    sys3(SYS_REPORT, SLOT_CONSOLE, 20, frames - f0);
    sys3(SYS_REPORT, SLOT_CONSOLE, 21, (frames - f0) * 100 * tsc_per_tick * 100 / (el ? el : 1));
    sys3(SYS_REPORT, SLOT_CONSOLE, 22, (tiles_sent - tl0) / n);
    sys3(SYS_REPORT, SLOT_CONSOLE, 23, worst * 1000000 / (tsc_per_tick * 100));
    /* phase C: every frame redraws the whole screen (all 300 tiles through SYS_PRESENT), 200 ticks */
    worst = 0; start = ticks(); f0 = frames; tl0 = tiles_sent; c_start = rdtsc();
    while (ticks() - start < 200 && !quit) { uint64_t a = rdtsc(); quit = poll_keys(); gc_invalidate_all(&canvas); frame(); uint64_t d = rdtsc() - a; if (d > worst) worst = d; }
    el = rdtsc() - c_start; n = frames - f0 ? frames - f0 : 1;
    sys3(SYS_REPORT, SLOT_CONSOLE, 25, frames - f0);
    sys3(SYS_REPORT, SLOT_CONSOLE, 26, (frames - f0) * 100 * tsc_per_tick * 100 / (el ? el : 1));
    sys3(SYS_REPORT, SLOT_CONSOLE, 27, (tiles_sent - tl0) / n);
    sys3(SYS_REPORT, SLOT_CONSOLE, 28, worst * 1000000 / (tsc_per_tick * 100));
    sys3(SYS_REPORT, SLOT_CONSOLE, 30, lscore * 100 + rscore);       /* 30: score */
    sys3(SYS_REPORT, SLOT_CONSOLE, 31, gc_ghost_hash(&canvas));      /* 31: what the game believes is on screen */
    sys3(SYS_REPORT, SLOT_CONSOLE, 32, presents);                    /* 32: frames presented in total */
    return 0;
}
