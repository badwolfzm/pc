/* Deterministic host differential for the six genuine Doom sscanf formats.
 * Inputs stay within supported numeric ranges; overflow is tested separately.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <limits.h>
int warden_scanf(const char *, const char *, ...);
static uint32_t state = UINT32_C(0x9e3779b9);
static uint32_t next(void) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}
static void compare(const char *input, const char *fmt) {
    int expected = 0x19730621;
    int actual = 0x19730621;
    int lhs = sscanf(input, fmt, &expected);
    int rhs = warden_scanf(input, fmt, &actual);
    if (lhs != rhs || (lhs == 1 && expected != actual)
        || (rhs != 1 && actual != 0x19730621)) {
        fprintf(stderr, "Mismatch: [%s] fmt [%s] libc %d/%d guest %d/%d\n",
                input, fmt, lhs, expected, rhs, actual);
        exit(1);
    }
}
int main(void) {
    char buffer[128];
    unsigned count = 0;
    const char *spaces[] = {"", " ", "\t", "  \n ", "\v"};
    const char *suffixes[] = {"", "junk", " ", "xyz", "\t"};
    for (unsigned i = 0; i < 1500; ++i) {
        unsigned v = next() & UINT32_C(0x7fffffff);
        unsigned style = next() % 6U;
        const char *ws = spaces[next() % 5U];
        const char *suffix = suffixes[next() % 5U];
        const char *fmt;
        int written;
        switch (style) {
        case 0:
            fmt = " 0x%x";
            written = snprintf(buffer, sizeof buffer, "%s0x%x%s", ws, v, suffix);
            break;
        case 1:
            fmt = " 0X%x";
            written = snprintf(buffer, sizeof buffer, "%s0X%X%s", ws, v, suffix);
            break;
        case 2:
            fmt = " 0%o";
            written = snprintf(buffer, sizeof buffer, "%s0%o%s", ws, v, suffix);
            break;
        case 3:
            fmt = " %d";
            written = snprintf(buffer, sizeof buffer, "%s%d%s", ws, (int)v, suffix);
            break;
        case 4:
            fmt = "%x";
            written = snprintf(buffer, sizeof buffer, "%s%x%s", ws, v, suffix);
            break;
        default:
            fmt = "%i";
            written = snprintf(buffer, sizeof buffer, "%s0x%x%s", ws, v, suffix);
            break;
        }
        if (written < 0 || (size_t)written >= sizeof buffer) return 2;
        compare(buffer, fmt);
        ++count;
    }
    if (count != 1500) return 3;
    puts("PASS: 1500 deterministic, differential, supported-format cases");
    return 0;
}
