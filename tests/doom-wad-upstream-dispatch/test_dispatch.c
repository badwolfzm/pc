/* Tests genuine pinned upstream W_OpenFile/W_Read dispatch, NOT Doom gameplay.
 * A one-IWAD guest profile binds upstream's stdc_wad_file symbol to the
 * capability-bounded Warden bridge; omit upstream w_file_stdc.c from link.
 */
#include "doom_wad_bridge.h"
#include "w_file.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const uint8_t fixture[32] = {
 'I','W','A','D',1,0,0,0,16,0,0,0,0,0,0,0,
 12,0,0,0,4,0,0,0,'D','A','T','A',0,0,0,0
};
static int deny_read, read_calls, open_calls;
static int read_provider(void *ctx, uint32_t offset, void *dst, size_t n) {
 (void)ctx;
 ++read_calls;
 if (deny_read || offset > sizeof fixture || n > sizeof fixture - offset) return 0;
 memcpy(dst,fixture+offset,n);
 return 1;
}
/* The exact symbol upstream w_file.c uses on the non-mmap codepath. */
static wad_file_t *open_bridge(char *p) { ++open_calls; return warden_doom_wad_class.OpenFile(p); }
static void close_bridge(wad_file_t *p) { warden_doom_wad_class.CloseFile(p); }
static size_t read_bridge(wad_file_t *p,unsigned int off,void *dst,size_t n) {
 return warden_doom_wad_class.Read(p,off,dst,n);
}
wad_file_class_t stdc_wad_file = {open_bridge,close_bridge,read_bridge};
int M_CheckParm(char *name) { (void)name; return 0; } /* disable mmap only */

int main(void) {
 char allowed[] = "DOOM1.WAD", disallowed[] = "DOOM2.WAD";
 unsigned char out[32] = {0};
 assert(warden_doom_wad_grant(allowed,NULL,read_provider,sizeof fixture));
 assert(W_OpenFile(disallowed)==NULL && open_calls==1);
 wad_file_t *wad=W_OpenFile(allowed);
 assert(wad && wad->mapped==NULL && wad->length==sizeof fixture);
 assert(open_calls==2 && read_calls>0);
 assert(W_OpenFile(allowed)==NULL); /* second simultaneous open denied */
 assert(W_Read(wad,0,out,12)==12 && memcmp(out,fixture,12)==0);
 assert(W_Read(wad,16,out,16)==16 && memcmp(out,fixture+16,16)==0);
 assert(W_Read(wad,31,out,2)==0);
 deny_read=1;
 assert(W_Read(wad,0,out,1)==0);
 deny_read=0;
 warden_doom_wad_revoke();
 assert(W_Read(wad,0,out,1)==0);
 W_CloseFile(wad);
 assert(W_OpenFile(allowed)==NULL);
 puts("PASS pinned upstream W_OpenFile dispatch calls authorized Warden WAD bridge");
 return 0;
}
