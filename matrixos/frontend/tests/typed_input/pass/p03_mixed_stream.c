/* walk a mixed stream: input cells are compared only to cells or ASCII literals,
 * the terminator is found by type, never by a value that input could spell */
#include <string.h>
int main(void) {
    const unsigned char wire[] = { 'a', 0x00, 0x11, 0x00, 0x00, 'b' };  /* 0x110000 spelled in bytes */
    mx_sym st[16]; mx_frame f = mx_frame_init(st, 16);
    if (mx_widen_bytes(mx_untrusted_bytes_new(wire, sizeof wire), 15, &f) != MX_OK) return 1;
    unsigned seen = 0, ctl = 0;
    for (uint32_t i = 0; i < f.len; i++) {
        mx_cell c;
        if (mx_sym_cell(f.cells[i], &c) != MX_OK) { ctl++; continue; }
        if (mx_cell_is(c, 'a') || mx_cell_is(c, 'b')) seen++;
    }
    return (seen == 2 && ctl == 1 && mx_frame_input_len(&f) == sizeof wire) ? 0 : 2;
}
