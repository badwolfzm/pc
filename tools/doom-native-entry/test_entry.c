#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

extern _Noreturn void warden_doom_test_start(void);
static jmp_buf jumped;
static volatile unsigned int create_calls;
static volatile unsigned int tick_calls;
static volatile int exit_status;
static volatile unsigned int exit_calls;

void doomgeneric_Create(int argc, char **argv) {
    create_calls++;
    assert(argc == 4);
    assert(argv != NULL && argv[4] == NULL);
    assert(strcmp(argv[0], "doomgeneric") == 0);
    assert(strcmp(argv[1], "-iwad") == 0);
    assert(strcmp(argv[2], "freedoom1.wad") == 0);
    assert(strcmp(argv[3], "-nogui") == 0);
    /* The game receives writable argv strings as required by C main ABI. */
    argv[0][0] = 'd';
}

void doomgeneric_Tick(void) {
    assert(create_calls == 1);
    tick_calls++;
    assert(tick_calls <= 300u);
}

_Noreturn void warden_doom_test_exit(int status) {
    exit_calls++;
    exit_status = status;
    longjmp(jumped, 1);
}

int main(void) {
    if (setjmp(jumped) == 0) {
        warden_doom_test_start();
        assert(!"native startup unexpectedly returned");
    }
    assert(create_calls == 1);
    assert(tick_calls == 300u);
    assert(exit_calls == 1);
    assert(exit_status == 0);
    puts("PASS: native entry invokes genuine API, 300 bounded ticks, clean thread exit");
    return 0;
}
