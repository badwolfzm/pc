#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <errno.h>

extern double warden_strtod(const char *, char **);

static const char *cases[] = {
    "1.00000000000000011102230246251565404236316680908203125",
    "1.000000000000000111022302462515654042363166809082031250000000000001",
    "1.00000000000000033306690738754696212708950042724609375",
    "1.000000000000000333066907387546962127089500427246093749999999999999",
    "1.7976931348623157e308", "1.7976931348623158e308",
    "1.7976931348623159e308", "4.9406564584124654e-324",
    "2.4703282292062327208828439643411068618252990130716238221279284125033774002813157396e-324",
    "-0.0000000000000000000", "0000000000000000000000000000001e-322",
    "0.000000000000000000000000000000000000000000000000000000000001e+10",
    "-1e-325", "1e309", "9.999999999999999999999999999999999999999999e-324",
    NULL
};

int main(void) {
    unsigned count = 0;
    for (unsigned i = 0; cases[i]; ++i) {
        char *end_a, *end_b;
        union { uint64_t u; double d; } a, b;
        errno = 0;
        a.d = warden_strtod(cases[i], &end_a);
        errno = 0;
        b.d = strtod(cases[i], &end_b);
        if (a.u != b.u || end_a - cases[i] != end_b - cases[i]) {
            fprintf(stderr, "FAIL case %u: %s bits %016llx / %016llx\n", i,
                cases[i], (unsigned long long)a.u, (unsigned long long)b.u);
            return 1;
        }
        ++count;
    }
    printf("PASS %u decimal midpoint, overflow, underflow and endpoint cases\n", count);
    return 0;
}
