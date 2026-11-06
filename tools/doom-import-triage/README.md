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
