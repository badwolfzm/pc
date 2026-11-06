#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include <errno.h>
extern double warden_strtod(const char*,char**);
static uint64_t state=0x1234567890abcdefULL;
static uint64_t next(void){return state=state*6364136223846793005ULL+1442695040888963407ULL;}
int main(int argc,char**argv){
 int n=argc>1?atoi(argv[1]):100000;
 int diff=0;
 for(int i=0;i<n;i++){
  char b[250];int at=0;
  if(next()&1)b[at++]='-';
  b[at++]='0';b[at++]='x';
  int len=1+next()%100,decpt=(int)(next()%(len+1));
  for(int j=0;j<len;j++){
   if(j==decpt)b[at++]='.';
   b[at++]="0123456789abcdef"[(next()>>32)&15];
  }
  if(decpt==len)b[at++]='.';
  if(next()&1){int exp=(int)(next()%2801)-1400;at+=sprintf(b+at,"p%+d",exp);}
  b[at]=0;
  char *ea,*eb;union{uint64_t u;double d;} a,c;
  errno=0;a.d=warden_strtod(b,&ea);
  errno=0;c.d=strtod(b,&eb);
  if(a.u!=c.u || ea-b!=eb-b){
    if(diff<10)fprintf(stderr,"DIFF %s -> %016llx vs %016llx (%td,%td)\n",b,(unsigned long long)a.u,(unsigned long long)c.u,ea-b,eb-b);
    diff++;
  }
 }
 fprintf(stderr,"HEX %d CASES: %d mismatches\n",n,diff);
 return diff?1:0;
}
