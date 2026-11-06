#include "adapter.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef struct { unsigned char bytes[64]; int refuse; unsigned calls; } source;
static void set32(unsigned char *p, uint32_t n) {
    for (unsigned i=0;i<4;++i) p[i]=(unsigned char)(n>>(8u*i));
}
static int read_at(void *context, uint32_t pos, void *dst, size_t n) {
    source *s=(source *)context;
    s->calls++;
    if (s->refuse || pos>64 || n>64-pos) return 0;
    memcpy(dst,s->bytes+pos,n); return 1;
}
int main(void) {
    source s={0};
    memcpy(s.bytes,"IWAD",4);set32(s.bytes+4,1);set32(s.bytes+8,32);
    memcpy(s.bytes+12,"DOOM!",5);
    set32(s.bytes+32,12);set32(s.bytes+36,5);
    memcpy(s.bytes+40,"E1M1",4);
    doom_wad_file f={0};uint32_t pos=99;char dst[8]={0};
    assert(doom_wad_file_open(&f,&s,read_at,48));
    assert(doom_wad_file_tell(&f,&pos) && pos==0);
    assert(doom_wad_file_seek(&f,12,DOOM_WAD_SEEK_SET));
    assert(doom_wad_file_read(&f,dst,5));
    assert(memcmp(dst,"DOOM!",5)==0);
    assert(doom_wad_file_tell(&f,&pos) && pos==17);
    assert(doom_wad_file_seek(&f,-2,DOOM_WAD_SEEK_CUR));
    assert(doom_wad_file_tell(&f,&pos) && pos==15);
    assert(doom_wad_file_seek(&f,-1,DOOM_WAD_SEEK_END));
    assert(doom_wad_file_tell(&f,&pos) && pos==47);
    assert(!doom_wad_file_seek(&f,1,DOOM_WAD_SEEK_END));
    assert(!doom_wad_file_seek(&f,INT64_MAX,DOOM_WAD_SEEK_CUR));
    assert(!doom_wad_file_seek(&f,INT64_MIN,DOOM_WAD_SEEK_CUR));
    assert(!doom_wad_file_seek(&f,0,7));
    assert(!doom_wad_file_read(&f,dst,2));
    assert(doom_wad_file_tell(&f,&pos) && pos==47);
    s.refuse=1;
    assert(!doom_wad_file_read(&f,dst,1));
    assert(doom_wad_file_tell(&f,&pos) && pos==47);
    s.refuse=0;
    assert(doom_wad_file_read(&f,dst,1));
    assert(doom_wad_file_seek(&f,0,DOOM_WAD_SEEK_END));
    assert(doom_wad_file_read(&f,0,0));
    assert(!doom_wad_file_read(&f,0,1));
    doom_wad_file_close(&f);
    assert(!doom_wad_file_tell(&f,&pos));
    assert(!doom_wad_file_read(&f,dst,1));
    assert(!doom_wad_file_seek(&f,0,DOOM_WAD_SEEK_SET));
    assert(!doom_wad_file_open(0,&s,read_at,48));
    s.refuse=1;assert(!doom_wad_file_open(&f,&s,read_at,48));
    assert(!doom_wad_file_tell(&f,&pos));
    puts("PASS Doom WAD fixed-memory file adapter");
    return 0;
}
