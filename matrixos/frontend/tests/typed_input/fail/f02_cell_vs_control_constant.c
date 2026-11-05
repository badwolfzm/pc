/* R4: an input cell cannot be compared with a bare constant (control or not) */
int f(mx_cell c) { return c == 0x110000; }
