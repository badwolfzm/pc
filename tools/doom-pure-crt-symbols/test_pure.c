#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stddef.h>
/* Do not rely on glibc's replacement functions in these test cases. */
static int sgn(int x) { return (x > 0) - (x < 0); }
int main(void) {
    assert(abs(0) == 0 && abs(-17) == 17 && abs(INT_MAX) == INT_MAX);
    double z = fabs(-0.0);
    assert(z == 0.0 && !signbit(z));
    assert(fabs(-1.25) == 1.25);
    assert(isinf(fabs(-INFINITY)) && !signbit(fabs(-INFINITY)));
    union { double d; uint64_t u; } nan = { .u = UINT64_C(0xfff800000000abcd) };
    union { double d; uint64_t u; } clear = { .d = fabs(nan.d) };
    assert(clear.u == UINT64_C(0x7ff800000000abcd));
    assert(strcasecmp("doom", "DOOM") == 0);
    assert(sgn(strcasecmp("Doom", "Doomx")) < 0);
    assert(sgn(strcasecmp("Z", "a")) > 0);
    assert(sgn(strcasecmp("abc", "abd")) < 0);
    const char hi[] = { (char)0xff, 0 };
    const char lo[] = { (char)0x80, 0 };
    assert(strcasecmp(hi, lo) > 0);
    assert(strncasecmp("AbC", "aBcdef", 3) == 0);
    assert(strncasecmp("a", "z", 0) == 0);
    assert(strncasecmp("a", "z", 1) < 0);
    assert(strncasecmp("abc", "abz", 2) == 0);
    assert(strncasecmp("ab", "abc", 3) < 0);
    const char s[] = "123abababcd";
    assert(strstr(s, "ababac") == NULL);
    assert(strstr(s, "abababcd") == s + 3);
    assert(strstr(s, "") == s);
    assert(strstr("", "") != NULL);
    assert(strstr("", "x") == NULL);
    char a[8] = { 0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55 };
    assert(strncpy(a, "hi", 5) == a);
    assert(a[0]=='h' && a[1]=='i' && a[2]==0 && a[3]==0 && a[4]==0 && a[5]==0x55);
    char b[4] = { 'x','x','x','x' };
    assert(strncpy(b, "abcdef", 3) == b);
    assert(b[0]=='a' && b[1]=='b' && b[2]=='c' && b[3]=='x');
    assert(strncpy(b, "ignored", 0) == b);
    puts("PASS: 6 pure freestanding symbols and edge semantics");
    return 0;
}
