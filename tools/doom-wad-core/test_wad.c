#include "wad.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t b[80];
static void u32(size_t i, uint32_t n) { for (unsigned k=0;k<4;++k) b[i+k]=(uint8_t)(n>>(k*8)); }
static void good(void) {
    memset(b,0,sizeof b); memcpy(b,"PWAD",4); u32(4,2); u32(8,32);
    memcpy(b+16,"abcd",4); u32(32,16); u32(36,4); memcpy(b+40,"MAP01",5);
    u32(48,20); u32(52,0); memcpy(b+56,"THINGS",6);
}
int main(void) {
    wad_view w={0}; const uint8_t *p=NULL; uint32_t n=0,ix=0;
    good(); assert(wad_open(&w,b,sizeof b)==0 && w.count==2);
    assert(wad_lump(&w,0,&p,&n)==0 && n==4 && memcmp(p,"abcd",4)==0);
    assert(wad_lump(&w,1,&p,&n)==0 && n==0);
    assert(wad_find_last(&w,"MAP01\0\0\0",&ix)==0 && ix==0);
    assert(wad_find_last(&w,"UNKNOWN\0",&ix)<0);
    assert(wad_lump(&w,2,&p,&n)<0 && p==NULL && n==0);
    good(); assert(wad_open(&w,b,11)<0 && w.bytes==NULL);
    good(); b[0]='X'; assert(wad_open(&w,b,sizeof b)<0);
    good(); u32(4,65537); assert(wad_open(&w,b,sizeof b)<0);
    good(); u32(8,UINT32_MAX); assert(wad_open(&w,b,sizeof b)<0);
    good(); u32(32,UINT32_MAX); assert(wad_open(&w,b,sizeof b)<0);
    good(); u32(36,UINT32_MAX); assert(wad_open(&w,b,sizeof b)<0);
    good(); u32(48,79); u32(52,2); assert(wad_open(&w,b,sizeof b)<0);
    good(); assert(wad_open(&w,b,sizeof b)==0);
    b[32]=255; assert(wad_lump(&w,0,&p,&n)<0 && p==NULL); /* detect post-validation mutation */
    assert(wad_open(NULL,b,sizeof b)<0);
    assert(wad_open(&w,NULL,sizeof b)<0);
    puts("PASS wad core: validated WAD directory, immutable view, bounded lookup, malformed input");
}
