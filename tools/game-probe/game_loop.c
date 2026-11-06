#include "game_loop.h"
void gp_game_init(gp_game *g) {
    gp_init(&g->player);
    g->input.read = 0u; g->input.count = 0u;
    g->display.valid = 0u; g->display.frame = 0u; g->held = 0u;
}
int gp_game_key(gp_game *g, uint8_t key, uint8_t down) {
    if (!g) return GT_ERR;
    return gt_input_push(&g->input, (gt_event){key, down});
}
int gp_game_step(gp_game *g, uint32_t *dirty) {
    if (!g || !dirty || g->display.frame == UINT64_MAX) return GT_ERR;
    gt_event event;
    while (gt_input_pop(&g->input, &event) == GT_OK) {
        if (event.down == 1u) g->held |= event.key;
        else g->held &= (uint8_t)~event.key;
    }
    gp_tick(&g->player, g->held);
    gp_render(&g->player, g->scratch);
    return gt_present(&g->display, g->scratch, sizeof g->scratch,
                      GP_W, GP_H, GP_W, dirty);
}
