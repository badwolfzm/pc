/* R7: input must not select code */
static void a(void) {} static void b(void) {}
static void (*const tab[])(void) = { a, b };
void f(mx_cell c) { tab[c](); }
