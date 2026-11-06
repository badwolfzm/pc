/* Narrow native Doom termination bridge: current Warden thread ONLY.
 * Warden ABI SYS_EXIT=6, not SYS_HOST_EXIT=7 (machine shutdown).
 *
 * Deliberately not general C exit(): this single-thread freestanding profile
 * has no atexit registry, open stdio streams or C++ static destructors.
 * Caller must finish any game-specific state saving before calling this.
 */
#include <stdint.h>

enum { WARDEN_SYS_EXIT = 6 };

#ifdef WARDEN_DOOM_EXIT_TEST
extern void warden_doom_test_exit_syscall(uint64_t number, uint64_t code);
static void thread_exit(uint64_t code) {
    warden_doom_test_exit_syscall(WARDEN_SYS_EXIT, code);
}
#else
static void thread_exit(uint64_t code) {
    __asm__ volatile("int $0x80"
                     :
                     : "a"((uint64_t)WARDEN_SYS_EXIT), "D"(code),
                       "S"((uint64_t)0), "d"((uint64_t)0)
                     : "memory", "rcx", "r11");
}
#endif

_Noreturn void exit(int status) {
    /* Preserve a 32-bit C status, without confusing a signed code with
     * pointer/capability slot arguments or the host-control syscall. */
    thread_exit((uint64_t)(uint32_t)status);
    for (;;) {
#ifndef WARDEN_DOOM_EXIT_TEST
        __asm__ volatile("pause" ::: "memory");
#endif
    }
}
