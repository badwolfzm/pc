#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Compare our strtol to system strtoll; x86-64 long and long long 64 bits. */
static void check(const char *s,int base){
    char *a,*b;
    errno=0;long x=strtol(s,&a,base);int ex=errno;
    errno=0;long long y=strtoll(s,&b,base);int ey=errno;
    if(x!=y||a-s!=b-s||ex!=ey){
       fprintf(stderr,"FAIL %s base %d: %ld %lld offsets %ld %ld errno %d %d\n",s,base,x,y,(long)(a-s),(long)(b-s),ex,ey);
       abort();
    }
}
int main(void){
    const char *samples[]={"", " ", "+", "-", "0", "00", "08", "0x", "0x2a",
       "  -0XffZ","+1234","-1234", "9223372036854775807",
       "9223372036854775808","-9223372036854775808",
       "-9223372036854775809","9999999999999999999999999999999999",
       "-9999999999999999999999999999999999","aBcDe","0777","-0x8000000000000000",
       "0x7fffffffffffffff","0x8000000000000000","++++1","   -12ABC","z","101101",
       "0xg","0Xf"," - 1"};
    const int bases[]={0,2,8,10,16,30,36};
    for(unsigned i=0;i<sizeof(samples)/sizeof(samples[0]);i++)
      for(unsigned j=0;j<sizeof(bases)/sizeof(bases[0]);j++)check(samples[i],bases[j]);
    char *end=NULL;
    errno=0;assert(strtol("123",&end,1)==0&&errno==EINVAL&&end==(char*)"123"); /* next verify with array to avoid literal pooling */
    char invalid[]="x"; errno=0;assert(strtol(invalid,&end,37)==0&&errno==EINVAL&&end==invalid);
    puts("PASS strtol C-locale/base/overflow/endptr against host strtoll");
    return 0;
}
