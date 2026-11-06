#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>

int warden_scanf(const char *, const char *, ...);

static int total;
static void check(const char *str, const char *fmt) {
    int expected = 1234567, got = 1234567;
    int n1 = sscanf(str, fmt, &expected);
    int n2 = warden_scanf(str, fmt, &got);
    if (n1 != n2 || (n1 == 1 && expected != got) || (n2 != 1 && got != 1234567)) {
        fprintf(stderr, "FAIL: [%s] [%s] libc %d/%d ours %d/%d\n",
                str, fmt, n1, expected, n2, got);
        exit(1);
    }
    ++total;
}
int main(void) {
    const char *fmt[] = {" 0x%x", " 0X%x", " 0%o", " %d", "%x", "%i"};
    const char *v[] = {" 0xFF", " 0X1a", " 077", " 012", " -33", " +21",
                       " 100", " -0", " 0", "12345", "bogus", "", "  ",
                       " 0x0", " 0X9", "0x100", " 010", "0x7fffffff"};
    for (size_t i = 0; i < sizeof(fmt)/sizeof(*fmt); ++i)
        for (size_t j = 0; j < sizeof(v)/sizeof(*v); ++j)
            check(v[j], fmt[i]);
    check("2147483647", " %d");
    check("-2147483648", " %d");
    int guard = 77;
    if (warden_scanf("123", "%s", &guard) != -1 || guard != 77) return 2;
    if (warden_scanf("123", "%n", &guard) != -1 || guard != 77) return 3;
    puts("PASS: 110 differential cases + unsupported-format guards");
    return 0;
}
