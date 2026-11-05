/* R1/R4: a plain integer cannot be assigned into an input cell */
mx_cell f(void) { mx_cell c; c = 0x110000; return c; }
