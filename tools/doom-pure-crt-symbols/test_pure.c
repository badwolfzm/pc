#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
/* Force real external call, not the compiler's libm intrinsic. */
int (*volatile abs_fn)(int) = abs;
double (*volatile fabs_fn)(double) = fabs;
int main(void) {
    assert(abs_fn(0) == 0 && abs_fn(-17) == 17);
    assert(abs_fn(INT_MAX) == INT_MAX);
    assert(fabs_fn(-1.25) == 1.25);
    assert(fabs_fn(-0.0) == 0.0 && !signbit(fabs_fn(-0.0)));
    assert(isinf(fabs_fn(-INFINITY)) && !signbit(fabs_fn(-INFINITY)));
    union { double d; uint64_t u; } negnan = {.u=UINT64_C(0xfff800000000abcd)};
    union { double d; uint64_t u; } posnan = {.d=fabs_fn(negnan.d)};
    assert(posnan.u == UINT64_C(0x7ff800000000abcd));
    puts("PASS: freestanding abs and fabs");
    return 0;
}
