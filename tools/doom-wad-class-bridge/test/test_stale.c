#include "doom_wad_bridge.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static const unsigned char a[16] = {'I','W','A','D',0,0,0,0,12,0,0,0,'A','A','A','A'};
static const unsigned char b[16] = {'I','W','A','D',0,0,0,0,12,0,0,0,'B','B','B','B'};
static int read_at(void *ctx, uint32_t off, void *dst, size_t len) {
    const unsigned char *src = ctx;
    if (off > 16 || len > 16 - off) return 0;
    memcpy(dst, src + off, len);
    return 1;
}
int main(void) {
    char one[] = "A.WAD", two[] = "B.WAD";
    unsigned char ch;
    assert(warden_doom_wad_grant(one, (void*)a, read_at, 16));
    wad_file_t *old = warden_doom_wad_class.OpenFile(one);
    assert(old && warden_doom_wad_class.Read(old, 12, &ch, 1) == 1 && ch == 'A');
    warden_doom_wad_class.CloseFile(old);
    assert(warden_doom_wad_grant(two, (void*)b, read_at, 16));
    wad_file_t *fresh = warden_doom_wad_class.OpenFile(two);
    assert(fresh && fresh != old);
    assert(warden_doom_wad_class.Read(old, 12, &ch, 1) == 0);
    warden_doom_wad_class.CloseFile(old);
    assert(warden_doom_wad_class.Read(fresh, 12, &ch, 1) == 1 && ch == 'B');
    warden_doom_wad_revoke();
    assert(warden_doom_wad_class.Read(fresh, 12, &ch, 1) == 0);
    assert(warden_doom_wad_grant(two, (void*)b, read_at, 16));
    fresh = warden_doom_wad_class.OpenFile(two);
    assert(fresh && fresh != old);
    assert(warden_doom_wad_class.Read(old, 12, &ch, 1) == 0);
    warden_doom_wad_class.CloseFile(old);
    assert(warden_doom_wad_class.Read(fresh, 12, &ch, 1) == 1);
    warden_doom_wad_class.CloseFile(fresh);
    int opens = 3;
    for (;;) {
        fresh = warden_doom_wad_class.OpenFile(two);
        if (!fresh) break;
        assert(fresh != old);
        warden_doom_wad_class.CloseFile(fresh);
        ++opens;
        assert(opens <= 64);
    }
    assert(opens == 64);
    assert(warden_doom_wad_class.OpenFile(two) == NULL);
    assert(warden_doom_wad_class.Read(old, 12, &ch, 1) == 0);
    puts("PASS: revoked/stale handles cannot access or close new grants, exhaustion fails closed");
    return 0;
}
