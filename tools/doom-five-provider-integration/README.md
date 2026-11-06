# Five genuine Doom runtime imports together (ET_REL only)

Issue #233. This integration-only test combines **five exact symbol providers** in a single pinned genuine Doomgeneric engine + merged Warden adapters partial link. It does not implement or modify any other agent's runtime code.

Providers:
- merged `tools/doom-native-ctype/doom_ctype.c` -> `__ctype_toupper_loc`
- exact draft PR #208 commit `fae610a1f640e232499f6d86b980640e8559c420` -> `__errno_location`
- merged `tools/doom-native-strtol/strtol.c` -> `strtol`
- exact draft canonical formatter PR #225 commit `a3529e43aff95423d0f7c12d78e33ab9e55e3fda` -> `snprintf`, `vsnprintf`

CI compiles the **real 79 original Doomgeneric source units** at `ozkl/doomgeneric@e257a09527062bfb986b6e5e0780839b6cbd5b71` with Warden's merged adapter components; creates one `ld -r` object; validates exactly the five expected unresolved symbols disappear, no new unresolved imports appear, and dangerous host file/process operations remain unresolved. The test checks every provider's global symbol definitions and undefined imports and rejects collisions or unexpected dependency imports. Machine-readable output includes object SHA-256s.

Running the local contract tests:

```sh
python3 -m unittest discover -s tools/doom-five-provider-integration -p 'test_*.py' -v
```

The GitHub CI workflow additionally clones pinned upstream and exact candidate sources, compiles and performs genuine ELF partial-link verification. Draft PR #225 and errno PR #208 are pinned as **test-only candidates**, not approved dependencies for production. Re-evaluate after they are reviewed/merged; never blindly replace their precise revisions.

Important: resulting combined object is **ET_REL**, not a final static ET_EXEC or native guest. This work cannot prove Warden can boot real Doom, render graphics, handle WAD, fit 512 MiB physical RAM or run Albion. Native Warden owner must integrate reviewed capabilities, build a real executable, boot twice under one vCPU with correct memory accounting, and review the changes independently. The five-symbol count is not additive to other reports unless measured in one link.

This task owns ONLY new `tools/doom-five-provider-integration/**` and `.github/workflows/doom-five-provider-integration.yml`; no kernel, host I/O, ABI, WAD, graphics or other agent-owned code modified.
