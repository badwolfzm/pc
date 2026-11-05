/* UTF-8 text from input is re-encoded by the validator; controls are refused */
#include <string.h>
int main(void) {
    const char good[] = "caf\xc3\xa9\n";
    const char bad[]  = "caf\x1b[31m";
    mx_sym st[32]; mx_frame f = mx_frame_init(st, 32);
    char out[32]; size_t n;
    if (mx_widen_utf8(mx_untrusted_bytes_new(good, strlen(good)), 31, &f, NULL) != MX_OK) return 1;
    if (mx_validate_text(&f, out, sizeof out, &n) != MX_OK || n != strlen(good) || memcmp(out, good, n)) return 2;
    if (mx_widen_utf8(mx_untrusted_bytes_new(bad, strlen(bad)), 31, &f, NULL) != MX_OK) return 3;
    if (mx_validate_text(&f, out, sizeof out, &n) != MX_E_SYNTAX) return 4;
    return 0;
}
