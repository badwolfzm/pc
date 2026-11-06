#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <errno.h>
extern double warden_strtod(const char *,char **);
static const char *samples[]={
 "0x1p-1074", "0x1p-1075", "0x1.0000000000001p-1075", "0x3p-1076",
 "0x1.8p-1075", "0x1.fffffffffffffp1023", "0x1.fffffffffffff8p1023",
 "0x1.fffffffffffffp+1024", "0x1p1024", "0X.8p-1073",
 "-0x1p-1075", "0x00000000000001.000000000000000000000001p0",
 "0x.000000000000000000000001p+10", "0x1.000000000000080000000000000p0",
 "0x1.000000000000180000000000000p0", "0x1.00000000000017fffffffffffp0",
 "0x0p-16000", "0x1p-16000", "0x1p16000", "0x1.0p16000",
 "0x1.p0", "0x0", "0Xf", "0x.8p-1", "0x0.00p99999",
 "0x1p+", "0x1p-", "0x1p0!", NULL
};
int main(void){int fail=0;for(int i=0;samples[i];i++){
 char *ea=0,*eb=0;union{uint64_t u;double d;} a,b;
 a.d=warden_strtod(samples[i],&ea);b.d=strtod(samples[i],&eb);
 if(a.u!=b.u || ea-samples[i]!=eb-samples[i]){
  fprintf(stderr,"HEX TEST %d %s -> %016llx vs %016llx\n",i,samples[i],(unsigned long long)a.u,(unsigned long long)b.u);++fail;
 }
}printf("HEX boundary tests %s\n",fail?"FAIL":"PASS");return !!fail;}
