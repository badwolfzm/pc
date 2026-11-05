#include "probe.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
int main(void) {
    gp_state a,b; static uint32_t p[GP_PIXELS], q[GP_PIXELS];
    gp_init(&a); gp_init(&b);
    assert(a.x == 80u && a.y == 50u);
    gp_render(&a,p); gp_render(&b,q);
    assert(gp_hash(p) == gp_hash(q));
    uint64_t initial=gp_hash(p);
    for (unsigned f=0;f<300;++f) { uint8_t keys=(f%60u<30u)?GP_LEFT:GP_RIGHT; gp_tick(&a,keys);gp_tick(&b,keys); }
    gp_render(&a,p);gp_render(&b,q);
    assert(a.frame == 300u && gp_hash(p)==gp_hash(q));
    assert(gp_hash(p) == initial); /* exactly reversible movement */
    for(unsigned k=0;k<300;++k)gp_tick(&a,GP_UP);
    assert(a.y==3u);
    for(unsigned k=0;k<300;++k)gp_tick(&a,GP_DOWN);
    assert(a.y==GP_H-4u);
    gp_render(&a,p);
    assert(gp_hash(p)!=initial);
    printf("PASS deterministic 300-frame replay; reversible movement; bounded coordinates; hash=%016" PRIx64 "\n",gp_hash(p));
    return 0;
}
