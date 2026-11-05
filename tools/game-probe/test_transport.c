#include "transport.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    static gt_surface surface;
    static uint32_t frame[GT_PIXELS];
    uint32_t dirty = 999u;
    assert(gt_present(&surface,frame,sizeof frame,GT_WIDTH,GT_HEIGHT,GT_WIDTH,&dirty)==GT_OK);
    assert(dirty==GT_TILE_COUNT && surface.frame==1u);
    assert(gt_present(&surface,frame,sizeof frame,GT_WIDTH,GT_HEIGHT,GT_WIDTH,&dirty)==GT_OK);
    assert(dirty==0 && surface.frame==2u);
    frame[GT_PIXELS-1u]=0xff12abcd;
    assert(gt_present(&surface,frame,sizeof frame,GT_WIDTH,GT_HEIGHT,GT_WIDTH,&dirty)==GT_OK);
    assert(dirty==1u && surface.pixels[GT_PIXELS-1u]==0xff12abcd);
    uint64_t oldframe=surface.frame;
    assert(gt_present(&surface,frame,sizeof frame-1u,GT_WIDTH,GT_HEIGHT,GT_WIDTH,&dirty)==GT_ERR);
    assert(gt_present(&surface,frame,sizeof frame,GT_WIDTH,GT_HEIGHT,GT_WIDTH+1u,&dirty)==GT_ERR);
    assert(gt_present(&surface,frame,sizeof frame,GT_WIDTH+1u,GT_HEIGHT,GT_WIDTH,&dirty)==GT_ERR);
    assert(surface.frame==oldframe);
    frame[0]=0xffca2211;
    assert(gt_present(&surface,frame,sizeof frame,GT_WIDTH,GT_HEIGHT,GT_WIDTH,&dirty)==GT_OK);
    assert(dirty==1u);
    frame[0]=0;
    assert(surface.pixels[0]==0xffca2211);
    gt_input q={0};
    gt_event ev;
    assert(gt_input_pop(&q,&ev)==GT_EMPTY);
    assert(gt_input_push(&q,(gt_event){16u,1u})==GT_ERR);
    assert(gt_input_push(&q,(gt_event){GT_KEY_LEFT,3u})==GT_ERR);
    for(unsigned i=0;i<GT_QUEUE;++i) assert(gt_input_push(&q,(gt_event){GT_KEY_RIGHT,1u})==GT_OK);
    assert(gt_input_push(&q,(gt_event){GT_KEY_LEFT,1u})==GT_FULL);
    for(unsigned i=0;i<GT_QUEUE;++i){assert(gt_input_pop(&q,&ev)==GT_OK);assert(ev.key==GT_KEY_RIGHT);}
    assert(gt_input_push(&q,(gt_event){GT_KEY_DOWN,2u})==GT_OK);
    assert(gt_input_pop(&q,&ev)==GT_OK && ev.key==GT_KEY_DOWN && ev.down==2u);
    assert(gt_input_pop(&q,&ev)==GT_EMPTY);
    puts("PASS transport: first frame, unchanged frame, two edge tiles, input bounds, 32-event wraparound");
    return 0;
}
