#include "wad_stream.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef struct { unsigned char data[64]; unsigned calls; int fail; } backing;
static void put32(uint8_t *p, uint32_t v) {for (unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
static int read_cb(void *ctx, uint32_t off, void *dst, size_t len) {
 backing *b=(backing*)ctx; ++b->calls;
 if(b->fail || off>sizeof(b->data) || len>sizeof(b->data)-off)return 0;
 memcpy(dst,b->data+off,len);return 1;
}
int main(void){
 backing b={0};wad_stream s;wad_lump lump;char buf[5]={0};
 memcpy(b.data,"IWAD",4);put32(b.data+4,1);put32(b.data+8,32);
 memcpy(b.data+12,"HELLO",5);put32(b.data+32,12);put32(b.data+36,5);memcpy(b.data+40,"TESTLUMP",8);
 assert(wad_open(&s,&b,read_cb,48));assert(s.opened && s.lump_count==1 && s.kind==1);
 assert(wad_lump_at(&s,0,&lump) && lump.offset==12 && lump.size==5 && memcmp(lump.name,"TESTLUMP",8)==0);
 assert(!wad_lump_at(&s,1,&lump));assert(wad_seek(&s,12));assert(wad_read(&s,buf,5));assert(memcmp(buf,"HELLO",5)==0);
 assert(s.cursor==17);assert(!wad_seek(&s,49));assert(!wad_read(&s,buf,40));assert(s.cursor==17);
 b.fail=1;assert(!wad_read(&s,buf,1));assert(s.cursor==17);b.fail=0;
 assert(wad_seek(&s,48));assert(wad_read(&s,0,0));assert(!wad_read(&s,buf,1));
 wad_close(&s);assert(!wad_read(&s,buf,1));assert(!wad_lump_at(&s,0,&lump));
 assert(!wad_open(&s,&b,read_cb,11));assert(!s.opened);
 assert(!wad_open(&s,&b,read_cb,WAD_MAX_SIZE+1u));
 put32(b.data+4, WAD_MAX_LUMPS+1u);assert(!wad_open(&s,&b,read_cb,48));
 put32(b.data+4,1);put32(b.data+8,45);assert(!wad_open(&s,&b,read_cb,48));
 put32(b.data+8,32);memcpy(b.data,"BAD!",4);assert(!wad_open(&s,&b,read_cb,48));
 memcpy(b.data,"PWAD",4);assert(wad_open(&s,&b,read_cb,48) && s.kind==2);
 put32(b.data+32,UINT32_MAX);assert(!wad_lump_at(&s,0,&lump));
 put32(b.data+32,12);put32(b.data+36,UINT32_MAX);assert(!wad_lump_at(&s,0,&lump));
 printf("PASS: bounded WAD stream / malformed-header / failed-I/O / cursor tests\n");return 0;
}
