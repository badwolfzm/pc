/* Host-side genuine upstream Doom frame probe using Warden callback mocks.
 * This is NOT a Warden guest and NOT an FPS benchmark. The actual upstream
 * doomgeneric_Create / doomgeneric_Tick are called with a freely supplied WAD.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include "doomgeneric.h"
#include "doomstat.h"
#include "doom_wad_bridge.h"

static jmp_buf completed;
static unsigned int frame_count;
static unsigned int startup_frames;
static unsigned int game_started;
static int no_input_control;
static int reverse_input;
static unsigned int key_press_sent, key_release_sent;
static unsigned int key_press_consumed, key_release_consumed;
static unsigned int level_frames, level_player_frames;
static int level_initial_x, level_initial_y, level_last_x, level_last_y;
static uint64_t ticks;
static uint64_t first_hash, last_hash;
static uint8_t *wad;
static size_t wad_len;

/* Linker --wrap intercepts real upstream I_GetEvent -> DG_GetKey calls,
 * forwarding to the *unaltered* Warden adapter and verifying delivery.
 * The upstream engine must consume both key transitions to pass.
 */
extern int __real_DG_GetKey(int *pressed, unsigned char *key);
int __wrap_DG_GetKey(int *pressed, unsigned char *key) {
    int result = __real_DG_GetKey(pressed, key);
    if (result && pressed && key && *key == (reverse_input ? 0xaf : 0xad)) {
        if (*pressed) ++key_press_consumed;
        else ++key_release_consumed;
    }
    return result;
}


static int read_at(void *ctx, uint32_t off, void *dst, size_t n) {
    (void)ctx;
    if ((!dst && n) || off > wad_len || n > wad_len - off) return 0;
    if (n) memcpy(dst, wad + off, n);
    return 1;
}
static uint64_t hash_pixels(const uint32_t *src, size_t n) {
    uint64_t h = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < n; ++i) {
        h ^= src[i];
        h *= UINT64_C(1099511628211);
    }
    return h;
}
uint64_t warden_doom_test_sys3(uint64_t num, uint64_t a, uint64_t b, uint64_t c) {
    (void)c;
    switch (num) {
    case 60: return (uint64_t)320 | ((uint64_t)240 << 16);
    case 63: return ++ticks; /* monotonic 10ms clock, synthetic host-time */
    case 62:
        if (no_input_control) return 0;
        /* PS/2 set-1 up-arrow press, then release. The actual Warden
         * DG_GetKey callback decodes these into Doom key semantics.
         */
        if (game_started && frame_count >= 2 && !key_press_sent) {
            key_press_sent = 1;
            return reverse_input ? 0x50 : 0x48;
        }
        if (game_started && frame_count >= 6 && !key_release_sent) {
            key_release_sent = 1;
            return reverse_input ? 0xd0 : 0xc8;
        }
        return 0;
    case 5: return 0;        /* yield */
    case 6:
        fprintf(stderr, "WARDEN ADAPTER EXIT code %llu\n", (unsigned long long)a);
        longjmp(completed, 2);
    case 61: {
        if (b == 0) return UINT64_MAX;
        const uint32_t *pixels = (const uint32_t *)(uintptr_t)b;
        if (!game_started) {
            ++startup_frames;
            if (startup_frames > 200) longjmp(completed, 3);
            return 0;
        }
        if (gamestate == GS_LEVEL) {
            ++level_frames;
            if (consoleplayer >= 0 && consoleplayer < MAXPLAYERS && players[consoleplayer].mo) {
                int x = players[consoleplayer].mo->x, y = players[consoleplayer].mo->y;
                if (!level_player_frames) { level_initial_x = x; level_initial_y = y; }
                level_last_x = x; level_last_y = y;
                ++level_player_frames;
            }
        }
        uint64_t hash = hash_pixels(pixels, 320U * 200U);
        if (frame_count == 0) first_hash = hash;
        last_hash = hash;
        ++frame_count;
        if (frame_count >= 10) longjmp(completed, 1);
        return 0;
    }
    default:
        fprintf(stderr, "unexpected Warden syscall %llu\n", (unsigned long long)num);
        return UINT64_MAX;
    }
}
int main(int argc, char **argv) {
    if (argc != 2 && !(argc == 3 && (strcmp(argv[2], "--no-input") == 0 || strcmp(argv[2], "--reverse-input") == 0))) {
        fprintf(stderr, "usage: %s DOOM1.WAD [--no-input|--reverse-input]\n", argv[0]); return 2;
    }
    no_input_control = (argc == 3 && strcmp(argv[2], "--no-input") == 0);
    reverse_input = (argc == 3 && strcmp(argv[2], "--reverse-input") == 0);
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror("fopen"); return 2; }
    if (fseek(f, 0, SEEK_END) || (long)(wad_len = (size_t)ftell(f)) < 12 ||
        wad_len > 64U * 1024U * 1024U || fseek(f, 0, SEEK_SET)) {
        fprintf(stderr, "WAD out of bounds\n"); fclose(f); return 2;
    }
    wad = malloc(wad_len);
    if (!wad || fread(wad, 1, wad_len, f) != wad_len) {
        fprintf(stderr, "WAD allocation/read failed\n"); fclose(f); return 2;
    }
    fclose(f);
    if (memcmp(wad, "IWAD", 4) || !warden_doom_wad_grant("DOOM1.WAD", NULL, read_at, (uint32_t)wad_len)) {
        fprintf(stderr, "WAD must be a valid, authorized IWAD\n"); return 2;
    }
    char *args[] = { "doom", "-iwad", "DOOM1.WAD", "-warp", "1", "-skill", "1",
                     "-nosound", "-nomusic", "-nojoy", "-noconfig", NULL };
    int jumped = setjmp(completed);
    if (jumped == 0) {
        doomgeneric_Create(10, args);
        game_started = 1;
        for (unsigned int n = 0; n < 3000; ++n) doomgeneric_Tick();
        fprintf(stderr, "frame deadline exhausted: %u frames\n", frame_count);
        return 1;
    }
    fprintf(stderr, "DOOM host game-state diagnostic: gamestate=%d level_frames=%u player_frames=%u xy_start=(%d,%d) xy_end=(%d,%d)\n",
            (int)gamestate, level_frames, level_player_frames, level_initial_x, level_initial_y, level_last_x, level_last_y);
    const int moved = level_initial_x != level_last_x || level_initial_y != level_last_y;
    const int common_ok = jumped == 1 && frame_count == 10 && gamestate == GS_LEVEL
        && level_frames == 10 && level_player_frames == 10;
    if (!common_ok) {
        fprintf(stderr, "FAIL actual-level gate: exit=%d gameplay=%u level_frames=%u player_frames=%u\n",
                jumped, frame_count, level_frames, level_player_frames);
        return 1;
    }
    if (no_input_control) {
        if (moved || key_press_sent || key_release_sent || key_press_consumed || key_release_consumed) {
            fprintf(stderr, "FAIL no-input control: player moved=%d, input sent/consumed=%u,%u,%u,%u\n",
                    moved, key_press_sent, key_release_sent, key_press_consumed, key_release_consumed);
            return 1;
        }
        printf("PASS real Doom no-input control frames=%u player_stationary=1 position=(%d,%d) first_hash=%016llx last_hash=%016llx (HOST MOCK ONLY)\n",
               frame_count, level_initial_x, level_initial_y, (unsigned long long)first_hash, (unsigned long long)last_hash);
        printf("DOOM_EVIDENCE_V1 mode=control frames=%u level_frames=%u player_frames=%u start_x=%d start_y=%d end_x=%d end_y=%d pressed=%u released=%u consumed_down=%u consumed_up=%u first_hash=%016llx last_hash=%016llx\n",
               frame_count, level_frames, level_player_frames, level_initial_x, level_initial_y, level_last_x, level_last_y,
               key_press_sent, key_release_sent, key_press_consumed, key_release_consumed,
               (unsigned long long)first_hash, (unsigned long long)last_hash);
    } else {
        if (!moved || !key_press_sent || !key_release_sent || !key_press_consumed || !key_release_consumed || first_hash == last_hash) {
            fprintf(stderr, "FAIL gameplay input/movement gate: moved=%d sent_down=%u sent_up=%u consumed_down=%u consumed_up=%u\n",
                    moved, key_press_sent, key_release_sent, key_press_consumed, key_release_consumed);
            return 1;
        }
        printf("PASS real upstream Doom engine startup_frames=%u gameplay_frames=%u keyboard_down=%u keyboard_up=%u consumed_down=%u consumed_up=%u level_frames=%u player_moved=1 first_hash=%016llx last_hash=%016llx (HOST MOCK ONLY)\n",
               startup_frames, frame_count, key_press_sent, key_release_sent, key_press_consumed, key_release_consumed, level_frames, (unsigned long long)first_hash, (unsigned long long)last_hash);
        printf("DOOM_EVIDENCE_V1 mode=%s frames=%u level_frames=%u player_frames=%u start_x=%d start_y=%d end_x=%d end_y=%d pressed=%u released=%u consumed_down=%u consumed_up=%u first_hash=%016llx last_hash=%016llx\n",
               reverse_input ? "reverse" : "intervention", frame_count, level_frames, level_player_frames, level_initial_x, level_initial_y, level_last_x, level_last_y,
               key_press_sent, key_release_sent, key_press_consumed, key_release_consumed,
               (unsigned long long)first_hash, (unsigned long long)last_hash);
    }
    warden_doom_wad_revoke();
    free(wad);
    return 0;
}
