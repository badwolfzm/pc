/* This test must be compiled against real pinned upstream w_file.h and w_file.c.
 * It intentionally mocks only the authorized storage provider and the argument
 * parser entry M_CheckParm(), not Doom's W_OpenFile/W_Read/W_CloseFile APIs.
 */
#include "doom_wad_bridge.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static const unsigned char data[32] = {
 'I','W','A','D',1,0,0,0,16,0,0,0,0,0,0,0,
 12,0,0,0,4,0,0,0,'T','E','S','T',0,0,0,0
};
static int deny;
static int provider(void *ctx, uint32_t at, void *out, size_t size) {
    (void)ctx;
    if(deny || at>sizeof data || size>sizeof data-at) return 0;
    memcpy(out,data+at,size);
    return 1;
}
int M_CheckParm(char *s) {
    (void)s; return 0;
}
int main(void) {
    char allowed[]="DOOM1.WAD", disallowed[]="DOOM2.WAD";
    unsigned char output[32]={0};
    assert(warden_doom_wad_grant(allowed,0,provider,sizeof data));
    assert(W_OpenFile(disallowed)==NULL);
    wad_file_t *f=W_OpenFile(allowed);
    assert(f && f->length==sizeof data);
    assert(W_OpenFile(allowed)==NULL);
    assert(W_Read(f,0,output,12)==12);
    assert(memcmp(output,data,12)==0);
    assert(W_Read(f,20,output,12)==12);
    assert(memcmp(output,data+20,12)==0);
    assert(W_Read(f,31,output,2)==0);
    deny=1;assert(W_Read(f,0,output,1)==0);deny=0;
    W_CloseFile(f);
    assert(W_Read(f,0,output,1)==0);
    f=W_OpenFile(allowed);
    assert(f);
    warden_doom_wad_revoke();
    assert(W_Read(f,0,output,1)==0);
    assert(W_OpenFile(allowed)==NULL);
    puts("PASS: pinned upstream W_OpenFile/W_Read/W_CloseFile linked to bounded Warden backend");
    return 0;
}
