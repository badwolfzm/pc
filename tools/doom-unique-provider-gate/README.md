# Genuine Doom — canonical merged runtime provider link gate

The canonical providers are now **merged into main**:

- PR #177 contributes only `abs` and `fabs` in `tools/doom-pure-crt-symbols/pure.c`
- PR #215 restores the four independently verified C-locale functions from the closed #175 into `tools/doom-mini-crt/mini_extra.c`
- PR #176 provides `malloc`, `calloc`, `realloc`, and `free` through bounded arena PR #113

This workflow uses these sources **directly from the checkout**, not obsolete
draft snapshots. Actual pinned upstream Doomgeneric and merged Warden adapters
are compiled as freestanding x86-64 objects. Before a genuine `ld -r` link,
the gate rejects duplicate strong definitions among all selected objects.
It then requires precisely the ten reviewed imports to disappear, zero new
unresolved imports and no masking of privileged host file/process functions.

A passing **ET_REL** is not a native ET_EXEC executable. A 1 MiB arena
is a candidate static reservation, not a measured Doom heap requirement.
Native Warden-loader placement, complete runtime, capability-scoped WAD
storage, actual guest resident RAM and QEMU gameplay still need independent
verification. This tool itself introduces no kernel or host-service authority.

Issue #184. Owns only `tools/doom-unique-provider-gate/**` and the workflow.
