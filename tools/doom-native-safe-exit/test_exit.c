/* Intentionally link against renamed provider, never hijack host libc exit(). */
#include <assert.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>

extern _Noreturn void warden_doom_exit(int status);
static jmp_buf escape;
static volatile uint64_t got_number;
static volatile uint64_t got_code;
static volatile unsigned int called;

void warden_doom_test_exit_syscall(uint64_t number, uint64_t code) {
    got_number = number;
    got_code = code;
    called++;
    longjmp(escape, 1);
}

static void verify(int status) {
    called = 0;
    got_number = UINT64_MAX;
    got_code = UINT64_MAX;
    if (!setjmp(escape)) {
        warden_doom_exit(status);
        assert(!"exit unexpectedly returned");
    }
    assert(called == 1);
    assert(got_number == 6);
    assert(got_number != 7); /* no SYS_HOST_EXIT / whole-machine stop */
    assert(got_code == (uint64_t)(uint32_t)status);
}

int main(void) {
    verify(0);
    verify(1);
    verify(42);
    verify(255);
    verify(-1);
    puts("PASS: Doom-specific exit terminates Warden thread via syscall 6 only");
    return 0;
}
