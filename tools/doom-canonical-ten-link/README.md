# Current non-overlapping freestanding Doom ten-import link (issue #182)

The merged PR #177 now deliberately exports **only `abs` and `fabs`**.
Draft PR #175 owns `strcasecmp`, `strncasecmp`, `strstr`, `strncpy`.
Draft PR #176 exports the four `malloc`/`calloc`/`realloc`/`free`
forwarders to the bounded draft arena #113.

Earlier draft integration PR #180 pinned the **obsolete six-function snapshot**
of #177, before those four string providers were removed. This test instead
stages the actual merged two-function source plus the independent exact-SHA
four-string and four-heap drafts. It checks source-object global symbol
definitions for duplication *before linking*, then compiles genuine upstream
Doom and Warden adapters with the existing `doom-native-link-gate` and
performs `ld -r`. Exactly ten unresolved imports must disappear, no new
undefined symbols may appear, and all nine protected host filesystem/process
symbols must **remain unresolved**, rather than being masked by dummy stubs.

Run unit tests with:

```sh
python3 -m unittest discover -s tools/doom-canonical-ten-link -p 'test_*.py' -v
```

The dedicated CI provides dependency staging and the exact pinned upstream
source. The generated `report.json` is real **ET_REL partial link evidence**:
it is NOT a bootable ELF, runnable Warden guest, complete libc, legal WAD
pipeline, physical RAM measurement, frame benchmark, or Albion support.

No kernel or another owner's CRT source is modified. This is a distinct
integration/regression gate, not a replacement for upstream feature PR
#175 or existing linker script ownership. All component owners must review
before final native linking. Task #182; branch
`agent/doom-canonical-ten-link-20261106`.
