#include "doom_wad_bridge.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static const unsigned char wad[32] = {
    'I','W','A','D', 1,0,0,0, 16,0,0,0, 0,0,0,0,
    12,0,0,0, 4,0,0,0, 'T','E','S','T',0,0,0,0
};
static int fail_reads;
static int reader(void *ctx, uint32_t off, void *dst, size_t n) {
    (void)ctx;
    if (fail_reads || off > sizeof wad || n > sizeof wad - off) return 0;
    memcpy(dst, wad + off, n);
    return 1;
}
int main(void) {
    char name[] = "DOOM1.WAD", wrong[] = "DOOM2.WAD";
    unsigned char buf[32];
    assert(!warden_doom_wad_grant("../DOOM1.WAD", NULL, reader, 32));
    assert(!warden_doom_wad_grant("/tmp/DOOM1.WAD", NULL, reader, 32));
    assert(!warden_doom_wad_grant("DOOM1.WAD", NULL, reader, 67108865));
    assert(warden_doom_wad_grant(name, NULL, reader, 32));
    assert(!warden_doom_wad_class.OpenFile(wrong));
    wad_file_t *f = warden_doom_wad_class.OpenFile(name);
    assert(f && f->length == 32 && f->mapped == NULL);
    assert(!warden_doom_wad_class.OpenFile(name));
    assert(warden_doom_wad_class.Read(f, 0, buf, 12) == 12);
    assert(!memcmp(buf, wad, 12));
    assert(warden_doom_wad_class.Read(f, 20, buf, 12) == 12);
    assert(!memcmp(buf, wad + 20, 12));
    assert(warden_doom_wad_class.Read(f, 31, buf, 2) == 0);
    assert(warden_doom_wad_class.Read(f, 0, NULL, 1) == 0);
    fail_reads = 1;
    assert(warden_doom_wad_class.Read(f, 0, buf, 1) == 0);
    fail_reads = 0;
    warden_doom_wad_class.CloseFile(f);
    assert(warden_doom_wad_class.Read(f, 0, buf, 1) == 0);
    f = warden_doom_wad_class.OpenFile(name);
    assert(f);
    warden_doom_wad_revoke();
    assert(warden_doom_wad_class.Read(f, 0, buf, 1) == 0);
    assert(!warden_doom_wad_class.OpenFile(name));
    assert(warden_doom_wad_grant(name, NULL, reader, 32));
    assert(warden_doom_wad_class.OpenFile(name));
    warden_doom_wad_revoke();
    puts("PASS: bounded authorized Doom w_file class bridge");
    return 0;
}
