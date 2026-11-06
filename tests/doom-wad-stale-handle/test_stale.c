/* Negative regression for merged #110: stale WAD file handles must not
 * acquire authority after a new grant/open. No real WAD or engine required. */
#include "doom_wad_bridge.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const uint8_t wad_a[32] = {
 'I','W','A','D',1,0,0,0,16,0,0,0,0,0,0,0,
 12,0,0,0,4,0,0,0,'A',0,0,0,0,0,0,0
};
static const uint8_t wad_b[32] = {
 'I','W','A','D',1,0,0,0,16,0,0,0,0,0,0,0,
 12,0,0,0,4,0,0,0,'B',0,0,0,0,0,0,0
};
static int reader(void *ctx, uint32_t offset, void *dst, size_t len) {
 const uint8_t *src = (const uint8_t *)ctx;
 if (!src || offset > 32 || len > 32 - offset) return 0;
 memcpy(dst, src + offset, len);
 return 1;
}
int main(void) {
 char a[]="A.WAD", b[]="B.WAD";
 uint8_t got=0;
 int failure=0;
 if (!warden_doom_wad_grant(a,(void *)wad_a,reader,32)) return 2;
 wad_file_t *old=warden_doom_wad_class.OpenFile(a);
 if (!old) return 3;
 warden_doom_wad_class.CloseFile(old);
 warden_doom_wad_revoke();
 if (!warden_doom_wad_grant(b,(void *)wad_b,reader,32)) return 4;
 wad_file_t *fresh=warden_doom_wad_class.OpenFile(b);
 if (!fresh) return 5;

 /* Pre-fix implementation reuses the same static pointer. Old must NOT
  * become a valid authority for B just because B has been opened. */
 if (warden_doom_wad_class.Read(old,24,&got,1)!=0) {
   fputs("FAIL: stale handle read from re-granted WAD\n",stderr);
   failure=1;
 }
 if (warden_doom_wad_class.Read(fresh,24,&got,1)!=1 || got!='B') {
   fputs("FAIL: fresh authorized handle cannot read B\n",stderr);
   failure=1;
 }
 /* A stale close must not revoke a fresh handle. */
 warden_doom_wad_class.CloseFile(old);
 if (warden_doom_wad_class.Read(fresh,24,&got,1)!=1 || got!='B') {
   fputs("FAIL: stale close invalidated a new handle\n",stderr);
   failure=1;
 }
 warden_doom_wad_revoke();
 if (!failure) puts("PASS stale handles cannot alias new grants");
 return failure;
}
