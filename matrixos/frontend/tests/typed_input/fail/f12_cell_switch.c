/* control flow on raw input: switch needs an integer, which a cell is not */
int f(mx_cell c) { switch (c) { case 'a': return 1; default: return 0; } }
