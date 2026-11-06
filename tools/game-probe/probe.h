#ifndef WARDEN_GAME_PROBE_H
#define WARDEN_GAME_PROBE_H
#include <stdint.h>
#define GP_W 160u
#define GP_H 100u
#define GP_PIXELS (GP_W * GP_H)
#define GP_MAX_EVENTS 32u
#define GP_LEFT 1u
#define GP_RIGHT 2u
#define GP_UP 4u
#define GP_DOWN 8u
typedef struct { uint32_t x, y, frame; uint8_t keys; } gp_state;
/* Caller owns exactly GP_PIXELS uint32_t entries. No allocation, host syscall or IO. */
void gp_init(gp_state *s);
void gp_tick(gp_state *s, uint8_t keys);
void gp_render(const gp_state *s, uint32_t *pixels);
uint64_t gp_hash(const uint32_t *pixels);
#endif
