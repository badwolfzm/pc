/* R5: an untrusted length cannot reach memcpy without mx_validate_len */
#include <string.h>
void f(char *dst, const char *src, mx_untrusted_u64 n) { memcpy(dst, src, n); }
