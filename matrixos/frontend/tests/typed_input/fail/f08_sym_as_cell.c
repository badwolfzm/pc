/* a mixed-stream symbol must go through mx_sym_cell (which refuses controls) */
mx_cell f(mx_sym s) { mx_cell c = s; return c; }
