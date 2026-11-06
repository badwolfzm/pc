/* Real pinned upstream Doomgeneric host gameplay test.
 * Implements platform callbacks only; engine itself is unmodified.
 * This is deliberately NOT Warden-native nor a one-vCPU performance test.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include "doomgeneric.h"
static unsigned int frames;
static uint64_t pixel_digest = UINT64_C(1469598103934665603);
static uint64_t start_ms;
static uint64_t monotonic_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) exit(5);
    return (uint64_t)ts.tv_sec*1000u + (uint64_t)ts.tv_nsec/1000000u;
}
void DG_Init(void) { start_ms=monotonic_ms(); }
void DG_DrawFrame(void) {
    if (!DG_ScreenBuffer) exit(6);
    for (unsigned i=0;i<(unsigned)DOOMGENERIC_RESX*DOOMGENERIC_RESY;++i) {
        uint32_t value=DG_ScreenBuffer[i];
        for (unsigned j=0;j<4;++j) {
            pixel_digest^=(value >> (j*8u))&255u;
            pixel_digest*=UINT64_C(1099511628211);
        }
    }
    ++frames;
}
void DG_SleepMs(uint32_t ms) {
    struct timespec req={(time_t)(ms/1000u),(long)(ms%1000u)*1000000L};
    while(nanosleep(&req,&req)!=0) {}
}
uint32_t DG_GetTicksMs(void) {return (uint32_t)(monotonic_ms()-start_ms);}
int DG_GetKey(int *pressed,unsigned char *key) {
    if(pressed) *pressed=0;
    if(key) *key=0;
    return 0;
}
void DG_SetWindowTitle(const char *title) {(void)title;}
int main(int argc,char **argv) {
    if(argc!=2) {fputs("usage: doom-real-host /absolute/freedoom1.wad\n",stderr);return 2;}
    char *args[]={"warden-freedoom-host","-iwad",argv[1],"-nosound","-nomusic","-skill","1","-warp","1","1",NULL};
    doomgeneric_Create(10,args);
    /* Drive real game loop; require actual framebuffer callbacks and a
       nontrivial pixel digest, not just successful engine initialization. */
    for(unsigned i=0;i<180 && frames<60;++i) {
        doomgeneric_Tick();
        DG_SleepMs(28);
    }
    printf("Genuine upstream Doom with Freedoom: presented=%u digest=%016llx\n",
        frames,(unsigned long long)pixel_digest);
    if(frames<10 || pixel_digest==UINT64_C(1469598103934665603))return 1;
    return 0;
}
