/* R7-ish: an input cell cannot become a command enumerator */
typedef enum { CMD_READ, CMD_WRITE } cmd_t;
cmd_t f(mx_cell c) { cmd_t k = c; return k; }
