# Genuine Doom: native Warden console output without host libc

This dedicated, original native runtime adapter closes two actual missing imports — `printf` and `puts` — using **Warden's existing native console capability syscall**. No Linux kernel, host libc, host `FILE*`, host `stdout`, host `system`, filesystem access, direct serial port I/O or machine shutdown are added.

The exact kernel contract on current `main` is `SYS_CONSOLE_WRITE=13`, with arguments (`console_slot`, `guest_buf`, `len`) on the native `int $0x80` ABI. Calls must use slot **0** only. The kernel enforces an Endpoint object with **WRITE** rights, copies the caller's mapped memory and caps each write at 1024 bytes. Warden's game loader must separately grant that console capability: **linking a console adapter does not itself confer rights.** Slot 1 remains the independent display capability in the existing Doomgeneric graphics adapter.

`printf` formats into a fixed 1024-byte stack buffer using the **already merged** bounded `vsnprintf` provider. It rejects failed/unsupported formatting and results of 1024 or more characters rather than logging truncated/misleading output. It makes no syscall for zero-length output. `puts` writes up to 1023 string bytes plus newline in one 1024-byte call; longer strings, NULL pointers, negative kernel results and short writes all return `-1`. No heap or implicit host output. Only ASCII / integer / string formatting supported by the selected `vsnprintf` works: e.g. `%p`, floating-point formats and positional formats remain **unsupported**. The native provider is not a complete libc/stdio replacement, and it never supplies `fprintf`/`vfprintf` or file write services.

## Evidence and reproduction

```sh
# Unit-test the real merged formatter and console with an injectable syscall:
cc -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic -fno-builtin \
  -fsanitize=undefined -fno-sanitize-recover=all \
  -Dsnprintf=warden_test_snprintf -Dvsnprintf=warden_test_vsnprintf \
  -c tools/doom-native-format/doom_format.c -o /tmp/warden-test-format.o
cc -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic -fno-builtin \
  -fsanitize=undefined -fno-sanitize-recover=all \
  -DWARDEN_DOOM_CONSOLE_TEST -Dprintf=warden_test_printf \
  -Dputs=warden_test_puts -Dvsnprintf=warden_test_vsnprintf \
  -c tools/doom-native-console/console.c -o /tmp/warden-test-console.o
cc -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic -fno-builtin \
  -fsanitize=undefined -fno-sanitize-recover=all \
  tools/doom-native-console/test_console.c \
  /tmp/warden-test-format.o /tmp/warden-test-console.o -o /tmp/test-doom-console
/tmp/test-doom-console
python3 -m unittest discover -s tools/doom-native-console -p 'test_*.py' -v
```

The dedicated CI clones pinned genuine `ozkl/doomgeneric@e257a09527062bfb986b6e5e0780839b6cbd5b71`, rebuilds merged 16-provider Warden native runtime via `tools/doom-native-runtime-bundle/compose.py`, compiles this console provider with freestanding flags, and **actually links** the original engine/adapter/bundle together via `ld -r`. The strict checker demands that **only** `printf` and `puts` disappear from the 21 previously unresolved imports, that `vsnprintf` is the only provider import and already satisfied, and that protected host file/process imports stay unresolved. All results are archived as JSON.

**Non-claims:** `ld -r` produces ET_REL (not a final executable); the console syscall is host-mocked in unit tests, not exercised in a Warden QEMU game boot. No genuine Doom keyboard/gameplay trace, no whole-guest 512 MiB resident RAM/FPS measurements, and no Albion Online execution. User stack addresses, capability grants, console-output needs during actual initialization, and unsupported `%p` formatting need integration-owner review before a runnable release.

Task `doom-native-console-output-20261106` claimed under issue #95. Owned **new** `tools/doom-native-console/**` and `.github/workflows/doom-native-console.yml` only; kernel, ABI, other agents' CRT, display, WAD and loader untouched. Requires independent review.
