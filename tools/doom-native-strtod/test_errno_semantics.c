/* Selected C-locale x86-64 glibc range tests: independent host comparison.
 * Round-to-nearest only. Boundary differences at rounded DBL_MIN exist and
 * are documented; this intentionally does not assert generic libc conformance.
 */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
extern double warden_strtod(const char *, char **);
struct fixture { const char *s; int want_errno; };
static const struct fixture cases[] = {
    {"0",0}, {"-0",0}, {"0x0",0},
    {"0x1p-1074",0}, {"0x1p-1073",0}, {"0x3p-1074",0},
    {"0x1p-1023",0}, {"0x1p-1022",0},
    {"0x1p-1075",ERANGE}, {"0x1.8p-1074",ERANGE},
    {"0x1.000000000001p-1074",ERANGE},
    {"5e-324",ERANGE}, {"1e-323",ERANGE}, {"1e-325",ERANGE},
    {"1e-309",ERANGE}, {"-1e-309",ERANGE}, {"2e-308",ERANGE},
    {"2.2250738585072014e-308",0},
    {"1.7976931348623157e308",0},
    {"1.7976931348623159e308",ERANGE}, {"1e309",ERANGE},
};
int main(void) {
    int failures=0;
    for (unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);i++) {
        const char *s=cases[i].s;
        char *ep=0,*hp=0;
        union { double d; uint64_t bits; } a,b;
        errno=0; a.d=warden_strtod(s,&ep); int got=errno;
        errno=0; b.d=strtod(s,&hp); int host=errno;
        if (got!=cases[i].want_errno || got!=host || a.bits!=b.bits || ep-s!=hp-s) {
            fprintf(stderr,"FAIL %s ours=%016llx errno=%d host=%016llx errno=%d offsets=%td/%td\n",
                s,(unsigned long long)a.bits,got,(unsigned long long)b.bits,host,ep-s,hp-s);
            failures++;
        }
    }
    printf("Selected range semantics: %zu cases; failures=%d\n",
       sizeof(cases)/sizeof(cases[0]),failures);
    return failures!=0;
}
