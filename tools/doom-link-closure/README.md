# Doom engine unresolved-symbol closure preflight

Reads the real pinned upstream host-object `inventory.json` from `tools/doom-upstream-compile` and candidate freestanding object file definitions through `nm -P`. It identifies exact symbol names still missing before attempting a Warden guest link.

```sh
python3 tools/doom-link-closure/check.py path/to/inventory.json path/to/runtime.o
python3 tools/doom-link-closure/check.py --strict path/to/inventory.json path/to/runtime.o
python3 -m unittest discover -s tools/doom-link-closure -p 'test_*.py' -v
```

Strict mode exits nonzero if any unresolved symbol remains. The inventory from Actions run 42179267544 confirms 80 real Doom source units compiled and 55 unresolved external symbol names. The separately tested local memory/string prototype matched 12 names, but that prototype is **not included or integrated** because other agents already own similar memory primitives in issues #116, #117, #120.

**This is name-only analysis, not a native ELF link, signature/ABI proof, security check, Warden QEMU boot, FPS or RAM measurement.** Host objects can contain libc fortify/PIE imports that differ from correctly freestanding builds. Calls such as `system` must not silently inherit host privileges; require an explicit deny/safe stub policy.

Scope is only `tools/doom-link-closure/**`, plus dedicated CI. No kernel, graphics, allocator, Doom engine, WAD, input or other agent-owned files changed. Relates to #95, merged #109, adapter #88/#111, allocator #113. Independent review and lead approval before merging.
