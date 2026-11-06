# Native Doom: six strictly pure C symbols

Task #174. Provide ONLY six freestanding functions required by genuine pinned
Doom C engine: `abs`, `fabs`, `strcasecmp`, `strncasecmp`,
`strstr`, and `strncpy`. No heap, syscalls, file/network interfaces,
process execution, dynamic loader, errno state or Warden privilege is added.
`strcasecmp` and `strncasecmp` implement **ASCII-only** case folding,
not arbitrary POSIX locale. `abs(INT_MIN)` is undefined as under ISO C.
`fabs` assumes x86-64 binary64 doubles to preserve NaN payload and clear
the sign of negative zero and NaNs.

Run the self-tests with `cc -std=c11 -O2 -ffreestanding -fno-builtin
-fno-stack-protector -fsanitize=undefined -fno-sanitize-recover=all pure.c
test_pure.c -o /tmp/test-pure && /tmp/test-pure`.

The dedicated CI also runs merged `tools/doom-native-link-gate/gate.py`
against pinned, genuine upstream Doom, and links the freestanding
`pure.o` into the **actual real-engine combined relocatable ELF**. The
machine-readable `check_link.py` requires exactly six unresolved pure
symbols to disappear, and requires sensitive host operations such as
`system`, `fopen`, `malloc` to remain unresolved. It never supplies
dangerous no-op permissions or fakes success for host service calls.

**This is not a native Warden boot.** Engine continues to import libc
format/IO/allocator/locale and other routines. Before a real ET_EXEC,
integrators must agree on runtime entry, memory layout, capability-backed
legal WAD, W^X and complete cross-toolchain ABI. Re-run exact CI and
independent security review before use in a guest. Paths exclusive to
`tools/doom-pure-crt-symbols/**` and dedicated CI.
