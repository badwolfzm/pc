/* Warden native ring-3 G0 game guest. Not installed into kernel boot yet.
 * Build with guest/build.sh; uses the existing Pong syscalls and ABI.
 * No Linux kernel, no libc, no host GPU, no privileged device access. */
#include "warden_bridge.h"
#include "rt.h"
static gb_bridge bridge;
static uint64_t guest_call(void *ctx, uint64_t nr, uint64_t a,
                           uint64_t b, uint64_t c) {
    (void)ctx;
    return sys3(nr,a,b,c);
}
int pong_main(void) {
    const uint64_t info=sys3(SYS_DISPLAY_INFO,SLOT_DISPLAY,0,0);
    if(info != (GB_W | ((uint64_t)GB_H<<16))) return 3;
    gb_init(&bridge);
    uint64_t next=sys3(SYS_TICKS,SLOT_DISPLAY,0,0);
    for(uint32_t frame=0;frame<60u;++frame) {
        /* Only a bounded number of scancodes per iteration. */
        for(unsigned n=0;n<32u;++n) {
            uint64_t raw=sys3(SYS_KEY,SLOT_DISPLAY,0,0);
            if(raw==0u) break;
            if((int64_t)raw<0 || gb_scancode(&bridge,(uint8_t)raw)!=GT_OK)
                return 4;
        }
        uint32_t dirty=0;
        if(gb_frame(&bridge,guest_call,0,&dirty)!=GT_OK) return 5;
        if(sys3(SYS_REPORT,SLOT_CONSOLE,70u,bridge.game.display.frame)!=0u)
            return 6;
        /* 10fps max paced by the existing 100Hz guest tick syscall.
           Yield while waiting, never spin inside a kernel syscall. */
        next+=10u;
        while(sys3(SYS_TICKS,SLOT_DISPLAY,0,0)<next)
            sys3(SYS_YIELD,0,0,0);
    }
    sys3(SYS_REPORT,SLOT_CONSOLE,71u,gp_hash(bridge.game.display.pixels));
    return 0;
}
