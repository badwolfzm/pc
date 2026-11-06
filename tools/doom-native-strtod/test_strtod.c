#define _GNU_SOURCE
#include <errno.h>
#include <math.h>
#include <float.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern double warden_strtod(const char *, char **);
static int total;
static int failed;
static void check(const char *s, int require_identical) {
  char *end_a = NULL, *end_b = NULL;
  errno = 0;
  double a = warden_strtod(s, &end_a);
  int er_a = errno;
  errno = 0;
  double b = strtod(s, &end_b);
  int er_b = errno;
  union { double f; uint64_t u; } va = {a}, vb = {b};
  int same = (end_a - s == end_b - s) &&
       ((isnan(a) && isnan(b)) || va.u == vb.u) &&
       (!require_identical || er_a == er_b);
  if (!same) {
    fprintf(stderr, "DIFF %s: 0x%016llx 0x%016llx end %ld/%ld errno %d/%d\n", s,
        (unsigned long long)va.u, (unsigned long long)vb.u,
        (long)(end_a - s), (long)(end_b - s), er_a, er_b);
    ++failed;
  }
  ++total;
}
int main(void) {
  const char *samples[] = {
    "", " ", "+", "-", "+x", "  123.25hi", "0", "-0", "+0", "000.000",
    "1", "1.5", "-2.75", ".5", "1.", "0.125", "00032.5", "3.141592653589793",
    "1.234567890123456", "-12345.6789", "1e2", "1E+2", "4.3e-2",
    "1e", "1e+", "2e-", "1e+3tail", "0x1", "0x1.8p+2", "0X.8p-1",
    "0x", "0x.hello", "0x1p", "-0x1a.8p3", "inf", "-INF", "infinite",
    "infinity", "-INFINITY", "nan", "NaN(foo_12)", "nan(", "nan(*bad)",
    "  -0.0", "6.25e+3", "0.0000125", "1e20", "1e-20", "1e-200",
    "1e308", "1e309", "1e-308", "1e-309", "1e-323", "1e-324", "1e-325",
    "2.2250738585072014e-308", "2.2250738585072011e-308", "1.7976931348623157e308",
    "0x1p-1074", "0x1p-1075", "0x1.fffffffffffffp+1023", "0x1p1024",
    "-0x1p-1074", "0x0.0000000000001p-1022", "0x1.0000000000001p-1022",
    "0x1p16384", "-0x1p16384", "0x1p-16384", "0x1p16383", "0x1p-16383"
  };
  for (unsigned i = 0; i < sizeof samples / sizeof *samples; ++i) check(samples[i], 0);
  for (int i = 1; i <= 10000; i += 31) {
    char b[64];
    snprintf(b, sizeof b, "%d.%06d", i, (i * 1009) % 1000000);
    check(b, 0);
  }
  fprintf(stderr, "compared %d representative C-locale strings; mismatches=%d\n", total, failed);
  return failed ? 1 : 0;
}
