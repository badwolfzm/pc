/* widen a byte string, validate it as a bounded decimal, use the trusted result */
#include <string.h>
int main(void) {
    const char wire[] = "4242";
    mx_sym st[16]; mx_frame f = mx_frame_init(st, 16);
    if (mx_widen_bytes(mx_untrusted_bytes_new(wire, strlen(wire)), 15, &f) != MX_OK) return 1;
    uint32_t port;
    if (mx_validate_u32_dec(&f, 1, 65535, &port) != MX_OK) return 2;
    return port == 4242 ? 0 : 3;
}
