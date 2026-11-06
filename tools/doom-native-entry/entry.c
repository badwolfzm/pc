/* Freestanding Doomgeneric native Warden startup candidate.
 * Kernel supplies a valid x86-64 user stack and maps the guest image.
 * IWAD filename must match a SEPARATELY installed capability grant.
 * Does not grant assets/files, allocate frames, or launch other processes.
 */
#include <stddef.h>

extern void doomgeneric_Create(int argc, char **argv);
extern void doomgeneric_Tick(void);
extern _Noreturn void exit(int status);

#ifdef WARDEN_DOOM_ENTRY_TEST
#define _start warden_doom_test_start
#define exit warden_doom_test_exit
extern _Noreturn void warden_doom_test_exit(int status);
#endif

#define WARDEN_DOOM_FRAME_LIMIT 300u
_Static_assert(WARDEN_DOOM_FRAME_LIMIT > 0u &&
               WARDEN_DOOM_FRAME_LIMIT <= 10000u, "invalid bounded run");

static char app_name[] = "doomgeneric";
static char iwad_flag[] = "-iwad";
static char iwad_name[] = "freedoom1.wad";
static char nogui_flag[] = "-nogui";
static char *game_argv[] = {
    app_name, iwad_flag, iwad_name, nogui_flag, NULL
};

_Noreturn void _start(void) {
    doomgeneric_Create(4, game_argv);
    for (unsigned int frame = 0; frame < WARDEN_DOOM_FRAME_LIMIT; ++frame) {
        doomgeneric_Tick();
    }
    exit(0);
    /* Failure-safe if a contract-breaking exit() were to return. */
    for (;;) {
#ifndef WARDEN_DOOM_ENTRY_TEST
        __asm__ volatile("pause" ::: "memory");
#endif
    }
}
