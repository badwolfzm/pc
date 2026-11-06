#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

int main(void) {
    assert(strcasecmp("DooM", "doom") == 0);
    assert(strcasecmp("ABC", "abd") < 0);
    assert(strcasecmp("abd", "ABC") > 0);
    assert(strcasecmp("\200", "\177") > 0);
    assert(strcasecmp("", "") == 0);
    assert(strcasecmp("A", "") > 0);
    assert(strncasecmp("Doom", "doom", 4) == 0);
    assert(strncasecmp("abc", "ABD", 2) == 0);
    assert(strncasecmp("abc", "ABD", 3) < 0);
    assert(strncasecmp("a", "b", 0) == 0);
    assert(strncasecmp("a", "", 5) > 0);
    /* Indirect call avoids host header fortify builtins in the test TU. */
    char *(*volatile copy_fn)(char *, const char *, size_t) = strncpy;
    char buf[9];
    for (size_t i=0; i<sizeof(buf); ++i) buf[i]='X';
    assert(copy_fn(buf,"hi",5)==buf);
    assert(buf[0]=='h' && buf[1]=='i' && buf[2]=='\0' && buf[4]=='\0' && buf[5]=='X');
    assert(copy_fn(buf,"abcdef",3)==buf);
    assert(buf[0]=='a' && buf[1]=='b' && buf[2]=='c');
    assert(copy_fn(buf,"",0)==buf);
    const char hay[]="ababa";
    assert(strstr(hay,"aba")==hay);
    assert(strstr(hay,"bab")==hay+1);
    assert(strstr(hay,"ba")==hay+1);
    assert(strstr(hay,"xyz")==NULL);
    assert(strstr(hay,"")==hay);
    assert(strstr("", "a")==NULL);
    assert(strstr("a", "a")!=NULL);
    puts("PASS freestanding C-locale string extras");
    return 0;
}
