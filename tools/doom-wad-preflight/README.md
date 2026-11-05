# Doom WAD preflight (host-side)

Read-only, standard-library-only verification of classic Doom WAD directory metadata. **Not a Doom engine, not an IWAD redistribution mechanism, not a Warden guest, and not a security proof.** Use legally obtained game assets; for free/open asset testing, choose an appropriately licensed IWAD such as Freedoom.

```sh
python3 tools/doom-wad-preflight/wad_preflight.py path/to/game.wad
python3 -m unittest discover -s tools/doom-wad-preflight -p 'test_*.py' -v
```

Limits: <=64 MiB file, <=65,536 lump entries. Rejects truncated headers, unknown signatures, out-of-file directories/lumps, empty/nonprintable lump names. Emits JSON (`format`, `bytes`, `lumps`, `map_markers`, `has_playpal`, `has_colormap`). Only metadata is read; lump contents are **not** parsed or authenticated. Overlapping lumps are not prohibited by this structural check. No performance or game-compatibility claims.

Next owner handoff: after selection of the actual Doom engine/port, integrate WAD access using Warden's bounded filesystem service and capability checks; never pass untrusted guest pointers directly to this Python tool. Do not equate this report with Doom boot success. One vCPU/one host process must be validated separately in actual QEMU runs (#67).
