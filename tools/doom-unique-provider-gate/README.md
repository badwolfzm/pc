# Genuine Doom — selected runtime strong-symbol provider gate

Task #184. The success of PR #180's ten-function ET_REL closure is real, but
its historical six-function PR #177 snapshot is not what was merged:
**merged PR #177 supplies abs/fabs ONLY**. The tested C string extras from
PR #175 are in a CLOSED, UNMERGED PR. This gate combines the *actual merged
abs/fabs source* with that *explicitly identified historical four-string
candidate*, plus draft #176 allocator aliases and #113 bounded arena.

This is **not** an attempt to reopen or merge someone else's PR. It is an
independent integration experiment on exact pinned candidate component
snapshots. The workflow verifies the current checkout's pure source against
the merged PR #177 SHA before using it. It temporarily restores only the
named candidate source files from the pinned commits, never edits shared
source on main.

For actual Doom pinned engine + real Warden WAD/graphics adapters, the test:
- builds C freestanding objects,
- parses real `nm -P -g --defined-only` output and **rejects duplicate
  strong global providers across ALL candidate and baseline objects**,
- requires unique known providers for exactly ten pure+allocator names,
- links all components with `ld -r`,
- requires exactly those ten undefined imports disappear; no new imports,
- requires system/file host calls remain undefined.

The result is **a candidate ET_REL**, not ET_EXEC; no entrypoint, privilege
allocation, guest linker-map proof, full 512 MiB guest accounting, or Doom
execution. The allocator includes a 1 MiB static arena of unknown adequacy.
Before final integration: obtain the D1 lead's decision on reusing #175
string implementation, independently approve #113/#176 heap, provide
capability-controlled WAD and process termination, run a truly final linker
and native QEMU test. Never link fake `system`/host file stubs.

Own only `tools/doom-unique-provider-gate/**` and the dedicated workflow.
