/* Freestanding, allocation-free, bounded printf subset. Unsupported => -1. */
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <limits.h>
struct output {char *s;size_t cap,n;int bad;};
static void put(struct output *o,char c){if(o->n>=INT_MAX){o->bad=1;return;}if(o->cap&&o->n<o->cap-1)o->s[o->n]=c;o->n++;}
static void pad(struct output *o,char c,unsigned n){while(n--&&!o->bad)put(o,c);}
static unsigned dec(const char **p){unsigned n=0;while(**p>='0'&&**p<='9'){unsigned d=(unsigned)(*(*p)++-'0');if(n>100000||n*10+d>1000000)return UINT_MAX;n=n*10+d;}return n;}
static uintmax_t magnitude(intmax_t v){return v<0?(uintmax_t)(-(v+1))+1U:(uintmax_t)v;}
static void number(struct output *o,uintmax_t n,unsigned base,int upper,unsigned width,int prec,int left,int zero,int alt,char sign){
 char rev[sizeof(uintmax_t)*8];unsigned len=0,z=0,prefix=0;
 const char *alphabet=upper?"0123456789ABCDEF":"0123456789abcdef";
 int iszero=n==0;
 do{rev[len++]=alphabet[n%base];n/=base;}while(n);
 if(prec==0&&iszero)len=0;
 if(prec>=0&&(unsigned)prec>len)z=(unsigned)prec-len;
 if(alt&&base==16&&!iszero)prefix=2;
 unsigned total=len+z+prefix+(sign?1U:0U),spaces=width>total?width-total:0;
 if(zero&&!left&&prec<0){z+=spaces;spaces=0;}
 if(!left)pad(o,' ',spaces);
 if(sign)put(o,sign);
 if(prefix){put(o,'0');put(o,upper?'X':'x');}
 pad(o,'0',z);while(len)put(o,rev[--len]);
 if(left)pad(o,' ',spaces);
}
int vsnprintf(char *restrict dst,size_t size,const char *restrict fmt,va_list ap){
 struct output o={dst,size,0,0};
 if(!fmt||(size&&!dst))return -1;
 for(const char *p=fmt;*p&&!o.bad;){
  if(*p!='%'){put(&o,*p++);continue;}
  ++p;if(*p=='%'){put(&o,*p++);continue;}
  int left=0,zero=0,plus=0,space=0,alt=0;
  for(;;){if(*p=='-')left=1;else if(*p=='0')zero=1;else if(*p=='+')plus=1;else if(*p==' ')space=1;else if(*p=='#')alt=1;else break;++p;}
  if(*p=='*'){o.bad=1;break;}
  unsigned width=dec(&p);if(width==UINT_MAX){o.bad=1;break;}
  int prec=-1;if(*p=='.'){++p;if(*p=='*'){o.bad=1;break;}unsigned v=dec(&p);if(v==UINT_MAX){o.bad=1;break;}prec=(int)v;}
  int length=0;
  if(*p=='h'){++p;length=1;if(*p=='h'){++p;length=2;}}
  else if(*p=='l'){++p;length=3;if(*p=='l'){++p;length=4;}}
  else if(*p=='z'){++p;length=5;}
  char conv=*p;if(!conv){o.bad=1;break;}++p;
  if(conv=='s'&&!length&&!plus&&!space&&!zero&&!alt){
   const char *s=va_arg(ap,const char *);if(!s){o.bad=1;break;}
   size_t n=0,lim=prec<0?SIZE_MAX:(size_t)prec;
   while(n<lim&&s[n])++n;
   unsigned spaces=width>n?width-(unsigned)n:0;
   if(!left)pad(&o,' ',spaces);
   for(size_t i=0;i<n&&!o.bad;i++)put(&o,s[i]);
   if(left)pad(&o,' ',spaces);
  }else if(conv=='c'&&!length&&prec<0&&!zero&&!plus&&!space&&!alt){
   unsigned spaces=width>1?width-1:0;
   if(!left)pad(&o,' ',spaces);
   put(&o,(char)va_arg(ap,int));if(left)pad(&o,' ',spaces);
  }else if(conv=='d'||conv=='i'||conv=='u'||conv=='x'||conv=='X'||conv=='o'){
   int signed_conv=conv=='d'||conv=='i';uintmax_t mag;char sign=0;
   if(signed_conv){
    intmax_t v;
    if(length==3)v=va_arg(ap,long);
    else if(length==4)v=va_arg(ap,long long);
    else if(length==5)v=va_arg(ap,ptrdiff_t);
    else {v=va_arg(ap,int);if(length==1)v=(short)v;if(length==2)v=(signed char)v;}
    sign=v<0?'-':plus?'+':space?' ':0;mag=magnitude(v);
    if(alt){o.bad=1;break;}
   }else{
    if(plus||space){o.bad=1;break;}
    if(length==3)mag=va_arg(ap,unsigned long);
    else if(length==4)mag=va_arg(ap,unsigned long long);
    else if(length==5)mag=va_arg(ap,size_t);
    else {mag=va_arg(ap,unsigned int);if(length==1)mag=(unsigned short)mag;if(length==2)mag=(unsigned char)mag;}
    if(conv=='o'&&alt){o.bad=1;break;}
   }
   number(&o,mag,conv=='x'||conv=='X'?16:conv=='o'?8:10,conv=='X',width,prec,left,zero,alt,sign);
  }else{o.bad=1;break;}
 }
 if(size&&dst)dst[o.n<size?o.n:size-1]='\0';
 return o.bad?-1:(int)o.n;
}
int snprintf(char *restrict dst,size_t size,const char *restrict fmt,...){
 va_list ap;va_start(ap,fmt);int n=vsnprintf(dst,size,fmt,ap);va_end(ap);return n;
}
