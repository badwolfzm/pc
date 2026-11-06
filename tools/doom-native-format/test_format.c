#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <stdarg.h>
static int via_v(char *s,size_t n,const char *f,...) {
 va_list ap;va_start(ap,f);int r=vsnprintf(s,n,f,ap);va_end(ap);return r;
}
int main(void){
 char s[100];
 assert(snprintf(s,sizeof(s),"%s %d %u %x","doom",-12,9U,255U)==13);
 assert(!strcmp(s,"doom -12 9 ff"));
 assert(snprintf(s,5,"abcdef")==6&&!strcmp(s,"abcd"));
 assert(snprintf(NULL,0,"%08x",42U)==8);
 assert(snprintf(s,sizeof(s),"%+08d",-42)==8&&!strcmp(s,"-0000042"));
 assert(snprintf(s,sizeof(s),"%-6.3s!","abcdef")==7&&!strcmp(s,"abc   !"));
 assert(snprintf(s,sizeof(s),"%lld",LLONG_MIN)>0&&!strcmp(s,"-9223372036854775808"));
 assert(via_v(s,sizeof(s),"%#x",255U)==4&&!strcmp(s,"0xff"));
 assert(via_v(s,sizeof(s),"%q",1)==-1);
 assert(via_v(s,sizeof(s),"%*d",2,3)==-1);
 assert(via_v(s,sizeof(s),"trailing%")==-1);
 assert(via_v(s,sizeof(s),"%.1000001s","x")==-1);
 assert(via_v(s,1,"%d",123)==3&&s[0]==0);
 assert(via_v(NULL,0,"%s","ok")==2);
 puts("format suite passed");
 return 0;
}
