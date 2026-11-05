#include "warden_bridge.h"
#include <assert.h>
#include <stdio.h>
typedef struct {uint32_t presents; uint64_t last_rect; const uint32_t *source; int deny;} mock;
static uint64_t invoke(void *ctx,uint64_t nr,uint64_t a,uint64_t b,uint64_t c) {
    mock *m=(mock*)ctx;
    if(nr==GB_DISPLAY_INFO_NR) return GB_W|((uint64_t)GB_H<<16);
    assert(nr==GB_PRESENT_NR && a==GB_DISPLAY_SLOT);
    m->last_rect=c; m->source=(const uint32_t*)(uintptr_t)b;
    ++m->presents; return m->deny? (uint64_t)-3:0u;
}
static gb_bridge b;
int main(void) {
    mock m={0};uint32_t dirty=0;
    gb_init(&b);
    assert(gb_scancode(&b,0xe0u)==GT_OK);
    assert(gb_scancode(&b,0x4du)==GT_OK); /* right arrow pressed */
    assert(gb_frame(&b,invoke,&m,&dirty)==GT_OK);
    assert(m.presents==1u && dirty==GT_TILE_COUNT);
    assert(b.game.player.x==81u);
    assert(m.source==b.screen && m.last_rect==((uint64_t)GB_W<<32 | (uint64_t)GB_H<<48));
    assert(b.screen[0]==0xff000000u && b.screen[(GB_H-1u)*GB_W]==0xff000000u);
    assert(b.screen[20u*GB_W]==b.game.display.pixels[0]);
    assert(b.screen[21u*GB_W+1u]==b.game.display.pixels[0]);
    assert(b.screen[219u*GB_W+319u]==b.game.display.pixels[GP_PIXELS-1u]);
    assert(gb_scancode(&b,0xe0u)==GT_OK);
    assert(gb_scancode(&b,0xcdu)==GT_OK); /* right arrow released */
    assert(gb_frame(&b,invoke,&m,&dirty)==GT_OK);
    assert(b.game.player.x==81u);
    assert(dirty==0u);
    assert(gb_scancode(&b,0x1eu)==GT_OK); /* A pressed */
    assert(gb_frame(&b,invoke,&m,&dirty)==GT_OK && b.game.player.x==80u);
    assert(gb_scancode(&b,0x9eu)==GT_OK);
    assert(gb_scancode(&b,0x2au)==GT_OK); /* unrelated shift */
    assert(gb_frame(&b,invoke,&m,&dirty)==GT_OK && b.game.player.x==80u);
    m.deny=1;
    assert(gb_frame(&b,invoke,&m,&dirty)==GT_ERR); /* syscall denial is propagated */
    assert(gb_frame(&b,0,&m,&dirty)==GT_ERR);
    assert(m.presents==5u);
    puts("PASS Warden adapter: PS/2 make/break, 320x240 scale, copied pixel base, syscall denial");
    return 0;
}
