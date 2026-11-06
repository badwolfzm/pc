# Genuine freestanding Doom strdup integration

Functional POSIX strdup, implemented with malloc, strlen and memcpy, without host file APIs or fake success stubs. On allocation failure it returns NULL. Unit tests cover empty and non-empty strings, independent copies, exact requested allocation bytes, allocation failure and defensive NULL input.

The combined ET_REL test stages the exact unchanged allocator sources from draft PR #113 (commit 315cc925ebf67f67d883bfa4d507ebcbb6b84c10), and exact heap closure scripts from draft PR #176 (e0b20c35a73a6e590c0b8dbe62910342f3d07527). It invokes merged PR #170 real Doom freestanding linker, then partially links the new strdup and checks it disappears from actual unresolved imports without removing sensitive host API requirements. Integration is for a proposed 1 MiB static heap only, with no claim it supports game startup.

No Warden native executable, native Doom guest boot, RAM high-water, input or FPS is demonstrated by ET_REL linkage. Owner claim doom-native-strdup-closure-20261106; only owns tools/doom-native-strdup-closure and its dedicated CI workflow. Requires independent review, heap PR dependencies, no automatic merge.
