#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>

extern int warden_stdio_grant_readonly(const char *,void *,int (*)(void *,uint32_t,void *,size_t),uint32_t);
extern void warden_stdio_revoke(void);
extern FILE *warden_test_fopen(const char *,const char *);
extern int warden_test_fclose(FILE *);
extern size_t warden_test_fread(void *,size_t,size_t,FILE *);
extern int warden_test_fseek(FILE *,long,int);
extern long warden_test_ftell(FILE *);

static const unsigned char input[] = "ABCDEFGH";
static int reads;
static int reader(void *ctx,uint32_t offset,void *dst,size_t len) {
    (void)ctx;
    if ((uint64_t)offset + len > sizeof(input)-1) return -1;
    memcpy(dst,input+offset,len);
    reads++;
    return 0;
}
int main(void) {
    assert(!warden_test_fopen("assets.wad","rb"));
    assert(warden_stdio_grant_readonly("../etc/passwd",0,reader,8) < 0);
    assert(warden_stdio_grant_readonly("assets.wad",0,reader,8) == 0);
    assert(warden_stdio_grant_readonly("assets.wad",0,reader,8) < 0);
    assert(!warden_test_fopen("different.wad","rb"));
    assert(!warden_test_fopen("assets.wad","wb"));
    FILE *s=warden_test_fopen("assets.wad","rb");
    assert(s);
    unsigned char buf[16]={0};
    assert(warden_test_fread(buf,2,3,s)==3);
    assert(memcmp(buf,"ABCDEF",6)==0 && warden_test_ftell(s)==6);
    assert(warden_test_fseek(s,-2,SEEK_CUR)==0);
    assert(warden_test_ftell(s)==4);
    assert(warden_test_fread(buf,1,16,s)==4);
    assert(memcmp(buf,"EFGH",4)==0);
    assert(warden_test_fread(buf,1,2,s)==0);
    assert(warden_test_fseek(s,LONG_MAX,SEEK_END)<0);
    assert(warden_test_fseek(s,-1,SEEK_SET)<0);
    assert(warden_test_fseek(s,0,SEEK_SET)==0);
    assert(warden_test_fread(buf,SIZE_MAX,2,s)==0);
    assert(warden_test_ftell(s)==0);
    assert(warden_test_fclose(s)==0);
    assert(warden_test_fclose(s)==EOF);
    assert(warden_test_ftell(s)==-1);
    FILE *s2=warden_test_fopen("assets.wad","r");
    assert(s2 && s2!=s);
    assert(warden_test_fread(buf,1,1,s)==0);
    assert(warden_test_fclose(s2)==0);
    warden_stdio_revoke();
    assert(!warden_test_fopen("assets.wad","rb"));
    assert(warden_stdio_grant_readonly("assets.wad",0,reader,8)<0);
    assert(warden_test_ftell(s2)==-1);
    assert(reads==2);
    puts("PASS: granted read-only file, seek, EOF, write denial, stale handle and revoke");
    return 0;
}
