/* R5: input cannot size a buffer */
int f(mx_cell c) { char buf[c]; buf[0] = 0; return buf[0]; }
