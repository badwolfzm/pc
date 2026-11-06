#include <assert.h>
#include <stdint.h>
#include <stdio.h>
extern const int32_t **__ctype_toupper_loc(void);
int main(void) {
    const int32_t **pp = __ctype_toupper_loc();
    assert(pp && *pp && pp == __ctype_toupper_loc());
    for (int c = 0; c < 256; ++c) {
        int expected = (c >= 'a' && c <= 'z') ? c - 'a' + 'A' : c;
        assert((*pp)[c] == expected);
    }
    assert((*pp)[-1] == -1);  /* EOF */
    /* x86-64 glibc C locale extends negative signed-char indices to 8-bit bytes. */
    for (int c = -128; c < -1; ++c) assert((*pp)[c] == c + 256);
    puts("PASS: 384-entry fixed C-locale table, all bytes and EOF");
    return 0;
}
