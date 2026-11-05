#ifndef WARDEN_GP_BRIDGE_H
#define WARDEN_GP_BRIDGE_H
#include "game_loop.h"
#define GB_W 320u
#define GB_H 240u
#define GB_PIXELS (GB_W * GB_H)
#define GB_PRESENT_NR 61u
#define GB_KEY_NR 62u
#define GB_DISPLAY_INFO_NR 60u
#define GB_DISPLAY_SLOT 1u
/* The adapter never accesses device memory: an explicitly provided
   capability-mediated syscall handles display and keyboard. */
typedef uint64_t (*gb_syscall)(void *context, uint64_t number, uint64_t a,
                              uint64_t b, uint64_t c);
typedef struct {
    gp_game game;
    uint32_t screen[GB_PIXELS];
    uint8_t e0_prefix;
} gb_bridge;
void gb_init(gb_bridge *bridge);
/* PS/2 set-1 make/break. Supports W/A/S/D and E0 arrow keys. */
int gb_scancode(gb_bridge *bridge, uint8_t scancode);
/* 2x nearest-neighbor pixels; 20px bars top and bottom. */
void gb_scale(gb_bridge *bridge);
/* One simulation step and a complete 320x240 screen copied through callback.
   Refuses absent callback, bad display geometry, or denied PRESENT. */
int gb_frame(gb_bridge *bridge, gb_syscall invoke, void *context,
             uint32_t *dirty);
#endif
