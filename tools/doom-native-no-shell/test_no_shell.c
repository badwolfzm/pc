#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>

extern int warden_no_shell_system(const char *command);
extern int *warden_test_errno_loc(void);

int main(void) {
    int *err = warden_test_errno_loc();
    assert(err != 0);
    *err = EINVAL;
    assert(warden_no_shell_system(0) == 0);
    assert(*err == EINVAL); /* system(NULL) must not clobber errno. */
    assert(warden_no_shell_system("/usr/bin/zenity --help >/dev/null 2>&1") == -1);
    assert(*err == ENOSYS);
    *err = 0;
    assert(warden_no_shell_system("touch /tmp/do-not-create") == -1);
    assert(*err == ENOSYS);
    /* The implementation must not dereference command bytes or execute it. */
    assert(warden_no_shell_system((const char *)(uintptr_t)1) == -1);
    assert(*err == ENOSYS);
    puts("PASS: system(NULL)=0; non-NULL fails ENOSYS; no shell or host effects");
    return 0;
}
