/* a filename from input reaches the trusted buffer only through the path validator */
#include <string.h>
int main(void) {
    const char wire[] = "level-07.bin";
    mx_sym st[300]; mx_frame f = mx_frame_init(st, 300);
    if (mx_widen_utf8(mx_untrusted_bytes_new(wire, strlen(wire)), 255, &f, NULL) != MX_OK) return 1;
    char name[64];
    if (mx_validate_path_segment(&f, name, sizeof name) != MX_OK) return 2;
    return strcmp(name, wire) == 0 ? 0 : 3;
}
