/* Warden user ABI for Phase 2b programs. Shared by the personality server,
 * the applications and the kernel-side loader (see src/apps.rs, which must
 * agree with the slot numbers and addresses here). */
#ifndef WARDEN_ABI_H
#define WARDEN_ABI_H

/* Kernel syscalls on int 0x80: rax = number, rdi/rsi/rdx/r10/r8 = args. */
#define WSYS_LOG 0           /* (console_slot, test_id, observed) */
#define WSYS_INVOKE 1
#define WSYS_MINT 2
#define WSYS_REVOKE 3
#define WSYS_RETYPE 4        /* (untyped_slot, kind, dst_slot, size[r10]) -> remaining */
#define WSYS_YIELD 5
#define WSYS_EXIT 6          /* (code) */
#define WSYS_HOST_EXIT 7     /* (host_slot, code) */
#define WSYS_SEND 8          /* (slot, w0, w1, w2, w3) blocks until received */
#define WSYS_RECV 9          /* (slot) -> rax=0, rdi=badge, rsi/rdx/r10/r8 = words */
#define WSYS_DELETE 10       /* (slot) -> nodes freed */
#define WSYS_MAP 11          /* (frame_slot, va, writable) -> 0 */
#define WSYS_BUDGET 12       /* (untyped_slot) -> remaining bytes */
#define WSYS_CONSOLE_WRITE 13 /* (console_slot, buf, len<=1024) -> len */
#define WSYS_SEND_NB 14      /* like SEND, but -8 (AGAIN) when nobody is receiving */
#define WSYS_CALL 15         /* (slot, w0, w1, w2, w3; r9 = reply_slot): send, then receive the reply */

/* Kernel error codes (negative in rax). */
#define WERR_INVALID (-1)
#define WERR_OCCUPIED (-2)
#define WERR_RIGHTS (-3)
#define WERR_REVOKED (-4)
#define WERR_BUDGET (-5)
#define WERR_KIND (-6)
#define WERR_QUEUE_FULL (-7)
#define WERR_AGAIN (-8)
#define WERR_FAULT (-9)

/* Capability slots. Applications: */
#define SLOT_CONSOLE 0       /* Endpoint, WRITE: only the server and the rogue hold it */
#define SLOT_PERSONALITY 1   /* app: WRITE on the personality endpoint, badge = client id; server: READ */
#define SLOT_REPLY 2         /* app: READ on its own reply endpoint */
#define SLOT_UNTYPED 3       /* app: 64 KiB untyped budget */
/* Personality server: WRITE on client i's reply endpoint at SRV_SLOT_REPLY0 + i.
 * Clients 1..3 are the native applications, 4..6 the Linux programs the
 * kernel's Linux personality forwards for (src/linux.rs); the server serves
 * both through the same protocol. */
#define SRV_SLOT_REPLY0 4
#define MAX_CLIENTS 8

/* Memory layout inside the 2 MiB user window 0x800000..0xA00000. */
#define APP_LOAD_BASE 0x800000UL
#define APP_LOAD_END 0x900000UL   /* ELF segments must end below this */
#define APP_BUF 0x900000UL        /* the application's view of its IPC buffer */
#define BUF_SIZE 0x4000UL         /* 16 KiB, four pages */
#define SRV_BUF_BASE 0x980000UL   /* server's view of client i's buffer: SRV_BUF_BASE + i*BUF_SIZE */
#define STACK_TOP 0xA00000UL      /* 64 KiB stack below this in every program */

/* IPC buffer: a 64-byte header of six syscall arguments, then data. */
#define BUF_DATA_OFF 64
#define BUF_DATA_MAX (BUF_SIZE - BUF_DATA_OFF)

/* Test ids reported with WSYS_LOG; the kernel's EXPECT table lists them. */
#define T_ROGUE_EBADF 100
#define T_ROGUE_OPEN_EACCES 101
#define T_ROGUE_SOCKET_EACCES 102
#define T_ROGUE_HUGE_LEN 103
#define T_ROGUE_ENOSYS 104
#define T_ROGUE_FLOOD 105
#define T_ROGUE_HOST_EXIT_EP 106
#define T_ROGUE_HOST_EXIT_REPLY 107
#define T_ROGUE_FOREIGN_SLOTS 108
#define T_ROGUE_FD_EXHAUST 109
#define T_SRV_CONNECTIONS 110
#define T_SRV_EXITS 111
#define T_ROGUE_OWN_BUF 112

/* Workload size, shared by httpd and fetch. */
#define N_REQUESTS 60
#define HTTP_PORT 8080
/* The Linux httpd listens here; the native one owns 8080. */
#define LINUX_HTTP_PORT 8081

#endif
