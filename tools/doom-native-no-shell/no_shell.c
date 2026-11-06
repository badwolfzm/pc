/* Narrow fixed no-shell contract for pinned native Doom's optional Zenity path.
 * Do not turn unavailable host command execution into fake success.
 * Not a general libc system() implementation or a way to grant capabilities.
 */
#include <errno.h>

int system(const char *command) {
    /* ISO C system(NULL): nonzero iff a command interpreter is available. */
    if (command == 0) return 0;
    /* Warden does not expose an arbitrary guest shell or host process. */
    errno = ENOSYS;
    return -1;
}
