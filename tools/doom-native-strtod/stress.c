#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <errno.h>
extern double warden_strtod(const char*,char**);
int main(void){
    uint64_t state=0x1234567890abcdefULL;
    int diff=0, n=1000000;
    for(int i=0;i<n;++i){
        char b[120]; int at=0;
        state=state*6364136223846793005ULL+1;
        if(state&1)b[at++]='-';
        state=state*6364136223846793005ULL+1;
        int len=1+(state%60);
        b[at++]=(char)('1'+(state%9)); b[at++]='.';
        for(int j=0;j<len;++j){
            state=state*6364136223846793005ULL+1;
            b[at++]=(char)('0'+(state%10));
        }
        state=state*6364136223846793005ULL+1;
        int exp=(int)((state>>16)%700)-350;
        at+=sprintf(b+at,"e%d",exp);
        errno=0;
        union{uint64_t u;double d;} v,w;
        w.d=strtod(b,0);v.d=warden_strtod(b,0);
        if(v.u!=w.u && !(isnan(v.d)&&isnan(w.d))){
            ++diff;
            if(diff<=8)fprintf(stderr,"diff %s libc %llx own %llx\n",b,
                (unsigned long long)w.u,(unsigned long long)v.u);
        }
    }
    fprintf(stderr,"stressed %d long decimal, %d binary precision mismatches\n",n,diff);
    /* A successful diagnostic process does not mean a conforming parser. */
    return diff != 0;
}
