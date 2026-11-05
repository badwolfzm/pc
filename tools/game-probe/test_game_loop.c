#include "game_loop.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
/* Two independently initialized instances must replay the same inputs identically. */
static gp_game a, b;
int main(void) {
    gp_game_init(&a); gp_game_init(&b);
    uint32_t da=0,db=0;
    assert(gp_game_step(&a,&da)==GT_OK && da==GT_TILE_COUNT);
    assert(gp_game_step(&b,&db)==GT_OK && db==GT_TILE_COUNT);
    assert(gp_hash(a.display.pixels)==gp_hash(b.display.pixels));
    assert(gp_game_key(&a,GT_KEY_RIGHT,1u)==GT_OK);
    assert(gp_game_key(&b,GT_KEY_RIGHT,1u)==GT_OK);
    for(unsigned f=0;f<300u;++f) {
        /* Alternate directions without game-specific syscall or framebuffer dependency. */
        if(f>0u && f%30u==0u) {
            uint8_t release = ((f/30u)&1u) ? GT_KEY_RIGHT : GT_KEY_LEFT;
            uint8_t press = ((f/30u)&1u) ? GT_KEY_LEFT : GT_KEY_RIGHT;
            assert(gp_game_key(&a,release,2u)==GT_OK);
            assert(gp_game_key(&b,release,2u)==GT_OK);
            assert(gp_game_key(&a,press,1u)==GT_OK);
            assert(gp_game_key(&b,press,1u)==GT_OK);
        }
        assert(gp_game_step(&a,&da)==GT_OK);
        assert(gp_game_step(&b,&db)==GT_OK);
        assert(da==db && da<=GT_TILE_COUNT);
        assert(gp_hash(a.display.pixels)==gp_hash(b.display.pixels));
        assert(a.player.x>=3u && a.player.x<GP_W-3u);
    }
    assert(a.player.frame==301u && a.display.frame==301u);
    assert(a.input.count==0u && b.input.count==0u);
    assert(gp_game_key(&a,GT_KEY_LEFT,0u)==GT_ERR);
    assert(gp_game_step(&a,0)==GT_ERR);
    printf("PASS G0 loop: 301 deterministic frames, copied surfaces, input replay; final hash=%016" PRIx64 "\n",gp_hash(a.display.pixels));
    return 0;
}
