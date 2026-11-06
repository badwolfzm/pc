#include "warden_bridge.h"
void gb_init(gb_bridge *b) {
    gp_game_init(&b->game);
    b->e0_prefix=0u;
}
int gb_scancode(gb_bridge *b, uint8_t scancode) {
    if (!b) return GT_ERR;
    if (scancode == 0xe0u) { b->e0_prefix=1u; return GT_OK; }
    uint8_t ext=b->e0_prefix;
    b->e0_prefix=0u;
    uint8_t code=scancode & 0x7fu, key=0;
    if (ext) {
        if(code==0x4bu) key=GT_KEY_LEFT;
        if(code==0x4du) key=GT_KEY_RIGHT;
        if(code==0x48u) key=GT_KEY_UP;
        if(code==0x50u) key=GT_KEY_DOWN;
    } else {
        if(code==0x1eu) key=GT_KEY_LEFT; /* A */
        if(code==0x20u) key=GT_KEY_RIGHT; /* D */
        if(code==0x11u) key=GT_KEY_UP; /* W */
        if(code==0x1fu) key=GT_KEY_DOWN; /* S */
    }
    if (!key) return GT_OK; /* Ignore unrelated keyboard input. */
    return gp_game_key(&b->game,key,(scancode & 0x80u)?2u:1u);
}
void gb_scale(gb_bridge *b) {
    for(uint32_t y=0;y<GB_H;++y) {
        for(uint32_t x=0;x<GB_W;++x) {
            uint32_t c=0xff000000u;
            if(y>=20u && y<220u) c=b->game.display.pixels[((y-20u)/2u)*GP_W+(x/2u)];
            b->screen[y*GB_W+x]=c;
        }
    }
}
int gb_frame(gb_bridge *b, gb_syscall invoke, void *context, uint32_t *dirty) {
    if(!b || !invoke || !dirty) return GT_ERR;
    uint64_t geom=invoke(context,GB_DISPLAY_INFO_NR,GB_DISPLAY_SLOT,0,0);
    if(geom != (GB_W | ((uint64_t)GB_H<<16))) return GT_ERR;
    if(gp_game_step(&b->game,dirty)!=GT_OK) return GT_ERR;
    gb_scale(b);
    const uint64_t rect=(uint64_t)GB_W<<32 | (uint64_t)GB_H<<48;
    uint64_t rc=invoke(context,GB_PRESENT_NR,GB_DISPLAY_SLOT,
                       (uint64_t)(uintptr_t)b->screen,rect);
    return rc==0u?GT_OK:GT_ERR;
}
