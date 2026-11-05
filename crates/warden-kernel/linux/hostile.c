/* The hostile Linux program: every tier of the Phase 0 table and every
 * boundary the personality enforces, each reported as a CHECK. It ends
 * with `int 0x80`, the native entry, which must kill it; the kernel then
 * tells the personality server it is gone. */
#include "sys.h"

void prog(void) {
    check(150, sys4(SYS_ptrace, 0, 0, 0, 0));                        /* denied tier */
    check(151, sys3(SYS_openat, -100, "/etc/passwd", 0));            /* fs.read: no grant at all */
    check(152, sys3(SYS_socket, 2, 1, 0));                           /* net: no grant */
    check(153, sys3(SYS_write, 1, 0x100000, 16));                    /* kernel memory as a buffer */
    check(154, sys6(SYS_mmap, 0, 64 << 20, 3, 0x22, -1, 0));         /* 64 MiB from a 1 MiB budget */
    i64 m = sys6(SYS_mmap, 0, 4096, 3, 0x22, -1, 0);
    check(155, m > 0 ? sys3(SYS_mprotect, m, 4096, 7) : m);          /* W^X */
    check(156, sys6(SYS_mmap, 0x100000, 4096, 3, 0x32, -1, 0));      /* MAP_FIXED over the kernel */
    check(157, sys0(999));                                           /* not a syscall */
    u64 brk0 = (u64)sys1(SYS_brk, 0);
    check(158, (u64)sys1(SYS_brk, 0x1FFF000) == brk0);               /* brk into the stack: refused */
    check(159, sys3(SYS_bpf, 0, 0, 0));                              /* denied tier */
    check(160, sys3(SYS_write, 7, "x", 1));                          /* unopened fd: the server says EBADF */
    char b[64];
    check(161, sys3(SYS_read, 3, b, 64));                            /* fd 3 is nobody's */
    check(162, sys3(SYS_write, 1, b, 1 << 20));                      /* 1 MiB from a 64-byte buffer */
    int alive = 1;
    for (int i = 0; i < 5000; i++)
        if (sys0(1000 + (i % 7)) != -38) alive = 0;
    check(163, alive);
    /* The native entry. A Linux thread has no business here: killed. */
    __asm__ volatile("mov $6, %%eax\n\txor %%edi, %%edi\n\tint $0x80" ::: "rax", "rdi", "memory");
    check(99, 0);
    exit_group(3);
}
