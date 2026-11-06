# Genuine Doom ten-import integration — ET_REL diagnostic, not a game

This experiment combines **real pinned original upstream Doomgeneric** with
the **merged Warden WAD/display adapters and mini CRT**, six pure-function
definitions from draft PR #177, and four bounded arena allocation aliases from
draft PR #176 backed by PR #113. Each dependency is restored into an ephemeral
CI checkout from an exact commit; no owner sources are edited.

The measured contract requires **exactly 10** missing imports to disappear
(`abs`, `fabs`, `strcasecmp`, `strncasecmp`, `strstr`, `strncpy`,
`malloc`, `calloc`, `realloc`, `free`) and zero new unresolved imports.
Host file/process functions, especially `system` and `fopen`, must remain
unresolved rather than being masked by permissive fake services. It compiles
the real upstream C and Warden objects with freestanding flags and performs
`ld -r` to compute linker symbol closure.

Reproduction after staging those dependencies:

```sh
python3 -m unittest discover -s tools/doom-native-ten-closure -p 'test_*.py' -v
python3 tools/doom-native-ten-closure/check.py --source /path/to/doomgeneric/doomgeneric --output target/doom-native-ten-closure
```

**Important limits:** The resulting ELF **ET_REL** is not a runnable guest
ELF, does not link a loader entrypoint, and does not run a game. A static
1 MiB candidate arena is included, but neither actual Doom heap demand nor
allocator safety, full guest memory usage, a 512 MiB limit, FPS, online
compatibility or WAD authorization is proven. Further guest integration
requires reviewed capability-backed file services, config/formatting,
explicit final ELF PT_LOAD/W^X placement and safe process termination.
No proprietary game assets are bundled.

Ownership: task #179, only `tools/doom-native-ten-closure/**` and its CI
workflow. No kernel, memory manager, Doom platform, or other agent files edited.
Keep draft pending CI, independent review and integration-owner approval.
