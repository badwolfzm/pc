# Real Doom default zone: 6 MiB needs more than a 1 MiB heap

Pinned genuine Doomgeneric `i_system.c` uses `DEFAULT_RAM = MIN_RAM = 6`
MiB and issues a six-megabyte `malloc` during zone setup. The merged
16-provider native partial-link gate currently compiles with **1 MiB**
of static heap capacity. That can never satisfy the default zone request.

This isolated test takes the **already merged** arena and its actual C
heap semantics, compiles an **8 MiB bounded profile**, and calls the real
`doom_alloc(6 MiB)`, writes both boundary bytes, then allocates another
1 MiB, observes bounded rejection of an impossible further 2 MiB request,
frees both allocations, and verifies reuse/coalescing. Host test uses
UBSan; it is not a stub or symbolic capacity comparison.

The pinned real Doom engine CI additionally uses the already merged
16-provider bundle script with exactly **two explicitly audited constants**
changed in a temporary generated copy under this new, isolated directory:
compile `-DDOOM_ARENA_BYTES=8388608` and record the true 8 MiB profile in
JSON. No existing agent-owned provider/build sources are modified. It
builds the same 79 original engine C translation units and 5 existing
Warden adapters, links all selected 16 runtime imports into one x86-64
`ET_REL`, rejects changed baseline imports and still protects host file
and process operations. It reads actual `SHF_ALLOC` ELF section bytes
including the eight-megabyte zero-filled heap.

**What this does not prove:** Doom may require more allocations on top of
the zone and gameplay may still fail to start; the native object is not
a final executable; the Warden loader may not map the larger image;
static ELF sections are not the guest's measured RSS and include neither
WAD data nor kernel/shared GPU/paging/stack; no QEMU native Doom run,
512 MiB guest test, FPS or Albion Online is demonstrated.

**Task #250**. Exclusive files `tools/doom-eight-mib-zone/**` and
`.github/workflows/doom-eight-mib-zone.yml`. Independent review before
selecting an 8 MiB production heap; no shared source change or merge.
