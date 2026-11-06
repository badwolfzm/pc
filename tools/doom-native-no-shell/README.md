# Genuine Doom: deny a host shell for optional error dialogs

The **actual pinned upstream** Doomgeneric source revision
`e257a09527062bfb986b6e5e0780839b6cbd5b71` has exactly two
`system()` call candidates in `i_system.c`: probing `/usr/bin/zenity`
and invoking the optional Zenity error dialog. This tool checks those
actual source callsites using the merged Warden lexical audit. It **fails
closed** if upstream invocation patterns drift.

The native provider implements `system(NULL)` returning **0** because
there is no guest shell, and all non-NULL commands return **-1 with
errno=ENOSYS**, through the merged single-thread
`__errno_location` provider. There is no shell, host process creation,
host filesystem, network, capability grant, command parsing or fake
success. The user must still obtain useful on-screen or serial errors
via separately reviewed Warden output services.

CI verifies the provider against hosted tests (including invalid command
pointer never dereferenced), builds it freestanding and checks genuine
pinned Doomgeneric + Warden adapter ET_REL link: precisely the original
`system` unresolved symbol disappears, no extra functions are
resolved, and host file APIs remain missing. This is a **compatibility
candidate for the pinned optional error-GUI code only**, NOT a general
implementation of arbitrary Linux shell functions.

**Not proven:** the patched routine is not a Warden executable, no
I_Error game shutdown/end-to-end serial verification, no actual QEMU
Doom frames, no 512 MiB guest resident high-water or FPS, no Albion
Online. Independent real call-path security review required before
integration. Other native libc imports remain unresolved. Draft and no
automatic merge.

Issue #260, exclusive `tools/doom-native-no-shell/**` and dedicated CI.
