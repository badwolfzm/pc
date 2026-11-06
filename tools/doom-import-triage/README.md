# Genuine Doom unresolved-import triage

Claim: #205 (`doom-import-triage-20261106`), unique path `tools/doom-import-triage/**`.

Unlike static inventories, this tool consumes ONLY a JSON result from the already merged **genuine pinned** `tools/doom-native-link-gate/gate.py`. It validates exact provenance/count coverage, rejects false executable/boot assertions, and generates a deterministic, source-linked priority queue: host/process/file boundary first, runtime state, memory, unknown externals, then math.

```sh
python3 tools/doom-native-link-gate/gate.py --source /pinned/doomgeneric/doomgeneric --output /tmp/doom-link
python3 tools/doom-import-triage/triage.py --gate /tmp/doom-link/gate.json --output /tmp/doom-triage.json
python3 -m unittest discover -s tools/doom-import-triage -p 'test_*.py' -v
```

This tool does **not** build an executable, implement missing symbols, grant permissions, download Doom, launch a guest, prove RAM fitting or measure FPS. Symbol labels are advisory and must not be used to silently substitute host libc/file/system calls. Unknown imports require manual provenance review. The original link report is an input, never rewritten. Verify caller paths and exact upstream source revision with the underlying gate. All tests are synthetic; a real linker report still requires the separately pinned source and environment.

Handoff: base main `4f05f4421b2602d5e7884770b90c440733a14cbc`. Docs/analysis-only contribution. Independent reviewer requested. Integrate when useful for actual #95 runtime ownership, not as evidence of a native Doom boot.

## CI genuine-source gate

The dedicated `.github/workflows/doom-import-triage.yml` workflow fetches and verifies the exact upstream `e257a09527062bfb986b6e5e0780839b6cbd5b71` commit, runs both parser suites, builds the 79 actual freestanding translation units and current merged Warden bridge objects using the existing linker gate, invokes this triage, checks exact symbol/provenance coverage and uploads both JSON reports. This is an **ET_REL link analysis only**; no guest executable, game frames or resource usage is asserted. Until a green run is observed, treat CI validation as pending.

## Security classification revision (#226)

Triaging genuine unresolved imports does not mean linking against host libc. This revision treats file/process/network/memory-management imports as **privileged host boundaries** requiring Warden-mediated services; `snprintf`/`vsnprintf` are **bounded formatting** functions and do not themselves grant host I/O, whereas `sprintf`/`vsprintf` require unbounded-output review. Fortified `__*_chk` helpers require real guest-side bounds checks; substituting unchecked copies violates the contract. A symbol classification is not evidence a safe implementation exists. The classification is intentionally conservative, incomplete and subject to review of each actual source call site. In particular, do not bypass kernel capability enforcement to close imports.

Acceptance: Python regression tests plus the existing pinned genuine Doom linker triage workflow, with exact symbol coverage. No binary linking or native Warden/QEMU boot is claimed by this revision.
