/* R1: pointing a plain integer pointer at a cell is refused (no implicit pointer conversion) */
uint32_t *f(mx_cell *c) { uint32_t *p = c; return p; }
