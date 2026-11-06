#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

extern const int32_t **__ctype_toupper_loc(void);
static int expected(int ch) {
    if (ch >= 'a' && ch <= 'z') return ch - ('a' - 'A');
    return ch;
}
int main(void) {
    const int32_t *table = *__ctype_toupper_loc();
    if (!table || table[EOF] != EOF) return 1;
    for (int ch = 0; ch <= UCHAR_MAX; ++ch) {
        int want = expected(ch);
        volatile int input = ch;
        if (table[ch] != want || toupper(input) != want) {
            fprintf(stderr, "incorrect toupper for %d: %d vs %d\n", ch, table[ch], want);
            return 2;
        }
    }
    /* Historical glibc signed-char accommodation is not the portable C API. */
    for (int ch = -128; ch < EOF; ++ch) {
        if (table[ch] != expected((unsigned char)ch)) return 3;
    }
    puts("PASS 256 C-locale bytes, EOF, and signed-char table coverage");
    return 0;
}
