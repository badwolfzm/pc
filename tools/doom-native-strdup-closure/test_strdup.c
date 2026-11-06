#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
extern char *strdup(const char *);
void *__real_malloc(size_t);
static int deny;
static size_t requested;
void *__wrap_malloc(size_t n){requested=n;return deny?NULL:__real_malloc(n);}
static int check(const char *s){
    size_t n=strlen(s);requested=0;
    char *p=strdup(s);
    if(!p||p==s||strcmp(p,s)||requested!=n+1)return 1;
    free(p);return 0;
}
int main(void){
    if(check("")||check("DOOM")||check("abcdefghijklmnopqrstuvwxyz0123456789"))return 1;
    deny=1;
    if(strdup("allocation-failure")!=NULL)return 2;
    deny=0;
    if(strdup(NULL)!=NULL)return 3;
    puts("PASS functional strdup");return 0;
}
