/* an input token selects a command only through the enum validator */
#include <string.h>
static int handlers_run[3];
static void do_read(void) { handlers_run[0]++; }
static void do_write(void) { handlers_run[1]++; }
static void do_render(void) { handlers_run[2]++; }
int main(void) {
    static const char *const names[] = { "read", "write", "render" };
    static void (*const fn[])(void) = { do_read, do_write, do_render };
    const char wire[] = "render";
    mx_sym st[16]; mx_frame f = mx_frame_init(st, 16);
    if (mx_widen_bytes(mx_untrusted_bytes_new(wire, strlen(wire)), 15, &f) != MX_OK) return 1;
    size_t idx;
    if (mx_validate_enum(&f, names, 3, &idx) != MX_OK) return 2;
    fn[idx]();                     /* idx is trusted: it came out of the validator */
    return handlers_run[2] == 1 ? 0 : 3;
}
