/* Single-thread, per-address-space C errno provider for freestanding Doom.
 * Deliberately NOT a general glibc TLS implementation.
 * Each separately linked native game image gets its own writable 4-byte cell.
 */
static int game_errno;

int *__errno_location(void) {
    return &game_errno;
}
