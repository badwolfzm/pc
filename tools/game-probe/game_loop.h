#ifndef WARDEN_GP_GAME_LOOP_H
#define WARDEN_GP_GAME_LOOP_H
#include "probe.h"
#include "transport.h"
/* One game instance; caller owns the whole instance. No host APIs or allocation. */
typedef struct {
    gp_state player;
    gt_input input;
    gt_surface display;
    uint32_t scratch[GP_PIXELS];
    uint8_t held;
} gp_game;
void gp_game_init(gp_game *game);
int gp_game_key(gp_game *game, uint8_t key, uint8_t down);
/* Steps the simulation once and publishes a copied frame; returns GT_OK or GT_ERR. */
int gp_game_step(gp_game *game, uint32_t *dirty);
#endif
