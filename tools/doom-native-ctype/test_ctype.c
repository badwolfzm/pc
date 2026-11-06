/* Compile ctype.c under a renamed symbol, so libc's toupper is independent. */
#include <assert.h>
#include <ctype.h>
#include <stdint.h>
#include <locale.h>
#include <stdio.h>

extern const int32_t **warden___ctype_toupper_loc(void);

static int independent_c_upper(int i) {
    if (i == -1) return -1;
    const unsigned u = (unsigned char)i;
    return u >= 'a' && u <= 'z' ? (int)u - ('a'-'A') : (int)u;
}

int main(void) {
    assert(setlocale(LC_CTYPE, "C") != NULL);
    const int32_t **first = warden___ctype_toupper_loc();
    assert(first != NULL && *first != NULL);
    assert(warden___ctype_toupper_loc() == first);
    for (int i = -128; i <= 255; ++i) {
        assert((*first)[i] == independent_c_upper(i));
        if (i >= 0) assert((*first)[i] == toupper(i));
    }
    assert((*first)[-1] == EOF);
    puts("PASS: C-locale uppercase table [-128..255], EOF, glibc reference (256 byte values)");
    return 0;
}
