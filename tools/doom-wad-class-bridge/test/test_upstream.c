/* Compile/link test against the genuine, pinned upstream Doomgeneric ABI.
 * W_OpenFile / W_CloseFile / W_Read come from upstream w_file.c, not mocks.
 * Test WAD bytes are synthesized locally; no engine or WAD is distributed.
 */
#include "doom_wad_bridge.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static const unsigned char wad[32] = {
    'I','W','A','D',1,0,0,0,16,0,0,0,0,0,0,0,
    12,0,0,0,4,0,0,0,'T','E','S','T',0,0,0,0
};
int M_CheckParm(char *check) { (void)check; return 0; }
static int read_at(void *ctx, uint32_t off, void *dst, size_t len) {
    (void)ctx;
    if (off > sizeof wad || len > sizeof wad - off) return 0;
    memcpy(dst, wad + off, len);
    return 1;
}
int main(void) {
    char allowed[] = "doom1.wad";
    char denied[] = "doom2.wad";
    unsigned char output[12] = {0};
    assert(warden_doom_wad_grant(allowed, NULL, read_at, sizeof wad));
    assert(W_OpenFile(denied) == NULL);
    wad_file_t *f = W_OpenFile(allowed);
    assert(f != NULL && f->file_class == &warden_doom_wad_class);
    assert(f->length == sizeof wad && f->mapped == NULL);
    assert(W_Read(f, 0, output, 12) == 12);
    assert(memcmp(output, wad, 12) == 0);
    assert(W_Read(f, sizeof wad, output, 1) == 0);
    W_CloseFile(f);
    assert(W_Read(f, 0, output, 1) == 0);
    f = W_OpenFile(allowed);
    assert(f != NULL);
    warden_doom_wad_revoke();
    assert(W_Read(f, 0, output, 1) == 0);
    puts("PASS: genuine upstream Doomgeneric w_file.c links to bounded Warden read backend");
    return 0;
}
