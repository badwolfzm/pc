# Warden-native Doom: process/thread exit (not machine shutdown)

New, isolated native **single-thread game** termination bridge. Doom's
`exit(int)` unresolved import binds to Warden's already defined
`SYS_EXIT=6` using the x86-64 `int $0x80` calling convention.
The bridge DOES NOT call `SYS_HOST_EXIT=7` or require a host-control
capability, so a normal game exit cannot deliberately stop the entire VM.

**Strictly narrow semantics:** This implementation does not run C
`atexit` hooks, flush C stdio streams or execute C++ destructors. It is
not a general libc `exit` replacement and should only be selected once
the actual Doom shutdown/save configuration control flow has been audited.
Any Warden thread-owned file/service cleanup remains the responsibility
of the kernel/personality owner, not the bare syscall adapter. If the
kernel were configured to stop QEMU when all threads exit, that is a
separate kernel lifecycle policy, not a SYS_HOST_EXIT invocation.

Host tests compile `exit.c` with a renamed exported function and
injected syscall callback to avoid overriding the test host's own libc.
Test cases cover codes 0, 1, 42, 255, -1 and assert call `6`, not
call `7`. A separate freestanding compilation requires **one** strong
export (`exit`) and **no unresolved symbols**. Genuine pinned upstream
`ozkl/doomgeneric@e257a09527062bfb986b6e5e0780839b6cbd5b71`
plus Warden adapters are compiled and partial-linked; exactly the real
`exit` import must disappear, with all host-privileged I/O imports
remaining unresolved.

This proves only a **freestanding relocatable object / ABI adapter**,
not a Warden guest executable or native gameplay boot. There is no
actual 512 MiB whole-guest RAM measurement, no FPS measurement, and no
Albion Linux-client compatibility claim.

Task #241, exclusive `tools/doom-native-safe-exit/**` and dedicated
workflow. Requires exact-head CI, independent kernel/security review,
and lead merge authorization; leave draft until approved.
