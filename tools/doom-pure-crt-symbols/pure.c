/* Narrow native Doom freestanding helpers: ONLY ISO C abs and fabs.
 * These introduce no privileges, heap, host OS calls or other imports.
 */
#include <stdint.h>
#include <limits.h>
#include <float.h>
_Static_assert(CHAR_BIT == 8 && sizeof(double) == 8 && DBL_MANT_DIG == 53,
               "Warden x86-64 IEEE-754 binary64 required");
/* abs(INT_MIN) is undefined by C; game caller must avoid it. */
int abs(int x) { return x < 0 ? -x : x; }
/* Bitwise fabs preserves NaN payload and clears signed zero's sign. */
double fabs(double x) {
    union { double d; uint64_t u; } bits = { .d = x };
    bits.u &= UINT64_C(0x7fffffffffffffff);
    return bits.d;
}
