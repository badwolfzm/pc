/* The hostile third application. It holds a console capability so it can
 * report what it observed with WSYS_LOG; the kernel's EXPECT table says what
 * each observation must be. It runs beside httpd and fetch and must not be
 * able to touch them, stop the machine, or make the kernel allocate. It ends
 * by reading another application's buffer through the address the server
 * uses for it, which in this address space is unmapped: #PF, thread killed. */
#include "libw.h"

static void report(long id, long v) { wsys(WSYS_LOG, SLOT_CONSOLE, id, v); }

int main(void) {
    /* 40: fd 3 is httpd's listening socket in httpd's table. Not in ours. */
    report(T_ROGUE_EBADF, lsys6(NR_read, 3, 0, 16, 0, 0, 0));
    /* 41: no fs.read grant. */
    report(T_ROGUE_OPEN_EACCES, sys_open("/site/index.html", O_RDONLY));
    /* 42: no net grant of either kind. */
    report(T_ROGUE_SOCKET_EACCES, sys_socket(AF_INET, SOCK_STREAM, 0));
    /* 43: a write whose length names a megabyte the buffer does not hold. */
    report(T_ROGUE_HUGE_LEN, lsys6(NR_write, 1, 0, 1 << 20, 0, 0, 0));
    /* 44: a syscall the personality does not know. */
    report(T_ROGUE_ENOSYS, lsys6(999, 0, 0, 0, 0, 0, 0));
    /* 45: 5000 of them. The kernel checks afterwards that its heap and
     * capability table did not grow; here we count any answer that was not
     * ENOSYS. */
    long bad = 0;
    for (int i = 0; i < 5000; i++)
        if (lsys6(998, i, 0, 0, 0, 0, 0) != -ENOSYS)
            bad++;
    report(T_ROGUE_FLOOD, bad);
    /* 46, 47: try to stop the machine with the capabilities we do hold. */
    report(T_ROGUE_HOST_EXIT_EP, wsys(WSYS_HOST_EXIT, SLOT_PERSONALITY, 0, 0));
    report(T_ROGUE_HOST_EXIT_REPLY, wsys(WSYS_HOST_EXIT, SLOT_REPLY, 0, 0));
    /* 48: the server's reply capabilities for the other apps live in the
     * server's slots 4..7; in our CSpace those names, and every other slot
     * up to 0xFFFF, name nothing. Count any that did not say so. */
    long named = 0;
    for (long s = 4; s < 0x10000; s = s < 16 ? s + 1 : s * 2)
        if (wsend_nb(s, 1, 2, 3, 4) != WERR_INVALID)
            named++;
    /* An ordinary IPC endpoint with WRITE must not impersonate the console.
     * The rogue has a real console in slot 0 and a separate writable
     * personality endpoint in slot 1. Both are mapped, valid endpoints.
     * A successful serial write is a security failure, even with WRITE.
     */
    volatile unsigned char *console_probe = (volatile unsigned char *)(APP_BUF + BUF_DATA_OFF);
    *console_probe = '.';
    if (wsys(WSYS_CONSOLE_WRITE, SLOT_PERSONALITY, (long)console_probe, 1) != WERR_KIND)
        named++;
    report(T_ROGUE_FOREIGN_SLOTS, named);
    /* 49: namespace epoll exhaustion is ours alone: at most 8 objects,
     * then ENOMEM; the other namespace retains its own capacity. */
    long opened = 0;
    for (int i = 0; i < 100; i++)
        if (sys_epoll_create1(0) >= 0)
            opened++;
    report(T_ROGUE_FD_EXHAUST, opened);
    /* 52: our own buffer is readable and writable: isolation, not a crash. */
    volatile uint64_t *mine = (volatile uint64_t *)(APP_BUF + BUF_DATA_OFF);
    *mine = 0xB0B;
    report(T_ROGUE_OWN_BUF, (long)*mine);
    /* Tell the server we are done, so it can account for us, then try to
     * read httpd's buffer where the server sees it. */
    lsys6(NR_exit_group, 0, 0, 0, 0, 0, 0);
    volatile uint64_t *theirs = (volatile uint64_t *)(SRV_BUF_BASE);
    long leaked = (long)*theirs; /* #PF here */
    report(99, leaked);          /* never reached; 99 is an unknown id and would FAIL */
    for (;;)
        ;
}
