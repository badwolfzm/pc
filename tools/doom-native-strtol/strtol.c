/* Self-contained numeric conversion logic; errno uses the existing
 * __errno_location import, NOT a fake shared global errno implementation.
 * C11 accepts bases 0 or 2..36. No host I/O/process/syscalls.
 */
#include <errno.h>
#include <limits.h>
#include <stddef.h>
static int ws(unsigned char c) { return c==' '||c=='\t'||c=='\n'||c=='\r'||c=='\f'||c=='\v'; }
static int digit(unsigned char c) {
    if(c>='0'&&c<='9')return c-'0';
    if(c>='a'&&c<='z')return c-'a'+10;
    if(c>='A'&&c<='Z')return c-'A'+10;
    return -1;
}
long strtol(const char *restrict nptr, char **restrict endptr, int base) {
    const char *p=nptr;
    if(base!=0 && (base<2||base>36)) {
        errno=EINVAL;
        if(endptr)*endptr=(char*)nptr;
        return 0;
    }
    while(ws((unsigned char)*p))++p;
    int negative=0;
    if(*p=='+'||*p=='-'){negative=*p=='-';++p;}
    if ((base==0||base==16)&&p[0]=='0'&&(p[1]=='x'||p[1]=='X')
        &&digit((unsigned char)p[2])>=0 && digit((unsigned char)p[2])<16) {
        base=16; p+=2;
    }
    if(base==0)base=*p=='0'?8:10;
    const unsigned long limit=negative ? (unsigned long)LONG_MAX+1UL : (unsigned long)LONG_MAX;
    const unsigned long q=limit/(unsigned long)base;
    const unsigned long rem=limit%(unsigned long)base;
    unsigned long acc=0;
    int any=0, overflow=0;
    for(;;++p){
        const int d=digit((unsigned char)*p);
        if(d<0||d>=base)break;
        any=1;
        if(!overflow){
            if(acc>q||(acc==q&&(unsigned long)d>rem))overflow=1;
            else acc=acc*(unsigned long)base+(unsigned long)d;
        }
    }
    if(endptr)*endptr=(char*)(any?p:nptr);
    if(!any)return 0;
    if(overflow){errno=ERANGE;return negative?LONG_MIN:LONG_MAX;}
    if(negative){
        if(acc==(unsigned long)LONG_MAX+1UL)return LONG_MIN;
        return -(long)acc;
    }
    return (long)acc;
}
