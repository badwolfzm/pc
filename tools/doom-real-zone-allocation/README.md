# Execute the real Warden Doom arena against upstream Doom's 6 MiB zone

Task [#248](https://github.com/badwolfzm/pc/issues/248), following merged PRs #238 and #243. The pinned original Doomgeneric `i_system.c` uses `I_ZoneBase -> AutoAllocMemory -> malloc(6*1024*1024)` by default, with minimum 6 MiB. The merged Warden heap alias calls `doom_alloc` in `tools/doom-freestanding-arena/arena.c`. This test **compiles that actual source**, unmodified, at 1 MiB and 8 MiB. It runs an additional 32 KiB allocation before the Doom-sized zone to model non-zone heap demand and verifies: at 1 MiB the 6 MiB zone fails without corrupting the prior allocation; at 8 MiB it succeeds, is writable, respects exhaustion, can be freed and reused, and restores accounting. Undefined-behavior sanitizer enabled.

```sh
python3 tools/doom-real-zone-allocation/run.py
```

The dedicated CI fetches exact upstream `ozkl/doomgeneric@e257a09527062bfb986b6e5e0780839b6cbd5b71`, verifies its default-zone source semantics using merged `tools/doom-zone-capacity-gate/gate.py`, checks that 1 MiB is rejected, 8 MiB meets the **necessary** nominal capacity requirement, and runs host C tests against the actual arena with UBSan.

**Limitations:** the host C test calls `doom_alloc`, which is the same function used by merged `malloc` alias, rather than linking a full upstream Doom process; sanitizer/linking constraints preclude replacing the host libc allocator globally in this test. A passing 8 MiB candidate is **not** a real guest configuration, a guarantee that other Doom allocations fit, a final ET_EXEC, a native Warden/QEMU launch, 512 MiB whole-guest accounting, FPS or Albion Online. A large static `.bss` arena also requires verified Warden guest address-space placement. Do not claim it resolves other 21 currently unresolved host APIs.

No source in arena, runtime bundle, kernel, WAD, or other agent ownership was modified. Deliver to the Doom integration/kernel owners for independent review before changing production allocation size or merging a final game binary.
