#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    char a[160], b[160];
    for(int n=0;n<128;++n) {
        for(int i=0;i<160;++i) a[i]=(char)(i+1),b[i]=(char)(i+1);
        assert(memcpy(a+16,b+20,(size_t)n)==a+16);
        for(int i=0;i<n;++i) assert(a[16+i]==b[20+i]);
        memset(a,0x81,(size_t)n);
        for(int i=0;i<n;++i) assert((unsigned char)a[i]==0x81);
        assert(memcmp(a,a,(size_t)n)==0);
        for(int off=-16;off<=16;++off) {
            for(int i=0;i<160;++i) a[i]=(char)(i+1),b[i]=(char)(i+1);
            int dest=32+off;
            if(dest<0 || dest+n>160 || 32+n>160) continue;
            char snap[128];
            for(int i=0;i<n;++i) snap[i]=a[32+i];
            assert(memmove(a+dest,a+32,(size_t)n)==a+dest);
            for(int i=0;i<n;++i) assert(a[dest+i]==snap[i]);
        }
    }
    const char hi[]="abc",aba[]="aba";
    assert(strlen(hi)==3);
    assert(strcmp("abc","abc")==0 && strcmp("abc","abd")<0 && strcmp("abd","abc")>0);
    assert(strcmp("\200","\177")>0);
    assert(strncmp("abc","abd",2)==0 && strncmp("abc","abd",3)<0 && strncmp("a","b",0)==0);
    assert(strchr(hi,'b')==hi+1 && strchr(hi,0)==hi+3 && strchr(hi,'z')==NULL);
    assert(strrchr(aba,'a')==aba+2 && strrchr(hi,0)==hi+3 && strrchr(hi,'x')==NULL);
    unsigned char x[2]={0,255},y[2]={0,1};
    assert(memcmp(x,y,2)>0 && memcmp(y,x,2)<0);
    puts("PASS freestanding Doom mini CRT: byte and string primitives");
    return 0;
}
