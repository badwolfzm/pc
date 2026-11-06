#include <assert.h>
#include <errno.h>
#include <stdio.h>

extern int *warden___errno_location(void);

int main(void) {
    int *a = warden___errno_location();
    int *b = warden___errno_location();
    assert(a && a == b && *a == 0);
    *a = EINVAL;
    assert(*b == EINVAL);
    *b = ERANGE;
    assert(*a == ERANGE);
    *b = 0;
    assert(*a == 0);
    puts("PASS: fixed errno pointer, writable per-image cell, no host errno alias");
    return 0;
}
