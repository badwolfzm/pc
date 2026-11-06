/* Freestanding C byte/string primitives for Warden Doom; no libc or heap. */
#include <stddef.h>
void *memcpy(void *restrict dst, const void *restrict src, size_t n) {
    unsigned char *d=(unsigned char *)dst;
    const unsigned char *s=(const unsigned char *)src;
    for (size_t i=0;i<n;++i) d[i]=s[i];
    return dst;
}
void *memmove(void *dst, const void *src, size_t n) {
    unsigned char *d=(unsigned char *)dst;
    const unsigned char *s=(const unsigned char *)src;
    if (d==s || n==0) return dst;
    /* Warden targets flat x86-64: integer-order comparisons avoid UB of
       relational pointer comparison between unrelated C objects. */
    if ((size_t)d < (size_t)s || (size_t)d-(size_t)s >= n) {
        for (size_t i=0;i<n;++i) d[i]=s[i];
    } else {
        for (size_t i=n;i>0;--i) d[i-1]=s[i-1];
    }
    return dst;
}
void *memset(void *dst, int byte, size_t n) {
    unsigned char *d=(unsigned char *)dst;
    for (size_t i=0;i<n;++i) d[i]=(unsigned char)byte;
    return dst;
}
int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *x=(const unsigned char *)a,*y=(const unsigned char *)b;
    for (size_t i=0;i<n;++i) if (x[i]!=y[i]) return (int)x[i]-(int)y[i];
    return 0;
}
size_t strlen(const char *s) { size_t n=0; while(s[n]) ++n; return n; }
int strcmp(const char *a, const char *b) {
    size_t i=0; while(a[i] && a[i]==b[i]) ++i;
    return (int)(unsigned char)a[i]-(int)(unsigned char)b[i];
}
int strncmp(const char *a, const char *b, size_t n) {
    for (size_t i=0;i<n;++i) {
        unsigned char x=(unsigned char)a[i], y=(unsigned char)b[i];
        if(x!=y || x==0) return (int)x-(int)y;
    }
    return 0;
}
char *strchr(const char *s,int c) {
    unsigned char t=(unsigned char)c;
    for(;;++s) {if((unsigned char)*s==t) return (char *)s; if(!*s) return NULL;}
}
char *strrchr(const char *s,int c) {
    unsigned char t=(unsigned char)c;
    const char *last=NULL;
    do { if((unsigned char)*s==t) last=s; } while(*s++);
    return (char *)last;
}
