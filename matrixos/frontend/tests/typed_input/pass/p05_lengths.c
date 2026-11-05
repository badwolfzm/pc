/* a length from the wire is checked against the real backing buffer before use */
#include <string.h>
int main(void) {
    unsigned char backing[64];
    memset(backing, 0xAA, sizeof backing);
    mx_untrusted_u64 wire_len = mx_untrusted_u64_new(80);   /* attacker says 80 */
    uint64_t len;
    if (mx_validate_len(wire_len, 16, sizeof backing, &len) == MX_OK) return 1;  /* must refuse */
    wire_len = mx_untrusted_u64_new(48);
    if (mx_validate_len(wire_len, 16, sizeof backing, &len) != MX_OK) return 2;
    memset(backing + 16, 0, (size_t)len);                    /* len is trusted now */
    return backing[15] == 0xAA && backing[16] == 0 && backing[63] == 0 ? 0 : 3;
}
