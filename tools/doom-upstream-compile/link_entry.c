/* Host-only link entry for the real Doom engine + original Warden callbacks.
 * DO NOT EXECUTE this binary: the adapter uses Warden int 0x80 syscalls,
 * which are not supported as intended on the Linux host. This is a
 * compile/link-only completeness check and is NOT a native guest image.
 */
#include "doomgeneric.h"

int main(int argc, char **argv) {
    doomgeneric_Create(argc, argv);
    for (;;) {
        doomgeneric_Tick();
    }
}
