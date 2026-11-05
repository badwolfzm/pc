# G0 reported-performance evidence gate

This is a **validation tool**, not a renderer, emulator, game benchmark, or proof of guest isolation. It enforces a minimal *reported* evidence shape for issue #67. It does not independently confirm that numbers are truthful. It is intentionally separate from PR #65 and PR #68 and owns only `tools/game-budget-gate/**`.

## Use

```sh
python3 -m unittest discover -s tools/game-budget-gate -p 'test_*.py' -v
python3 tools/game-budget-gate/validate.py measurements.json
```

The input is UTF-8 JSON, maximum 1 MiB. Fields:

- `target`: `{"platform":"warden-qemu","game_processes":1}`. OS services can be separate processes. This is not a claim that QEMU has only one host process or thread.
- `qemu`: `{"vcpus":1,"mode":"tcg","cpu":"qemu64","version":"8.2"}`. Modes: tcg/kvm/whpx; report the actual one.
- `limits`: positive integer `guest_ram_cap_bytes` and `host_rss_cap_bytes`. Account for full guest memory, not just game heap.
- `runs`: precisely two independent records, each with `boot_ok:true`, `clean_exit:true`, nonnegative `warmup_frames`, `timed_frames` >= 300 (max 100000), `frametimes_ms` array with one finite positive value per timed frame, `guest_peak_bytes`, `host_peak_rss_bytes`, and a lowercase 16-character hexadecimal `frame_digest`. Replay hashes must match.

This deliberately does **not** impose an arbitrary target FPS: low FPS is permitted by the user's goal. Report measured FPS as 1000 divided by mean frame time, and percentiles with the calculation method. Time samples are measured evidence, not fabricated fixtures. A validation PASS means only the JSON meets basic constraints. Source provenance, binary hash, toolchain/build flags, run logs, frame output, host CPU, guest process topology, capability denial tests and a reviewer are still necessary for G0 acceptance. The CPU framebuffer probes cannot satisfy G0 until they are actually integrated into Warden and executed under `-smp 1`.

## Coordination handoff

Claim: #69; base `b2eb17f1c5c0e57a57f6c672c793cc3638d0c2a9`; owned paths `tools/game-budget-gate/**`. Dependents: #67, roadmap PR #63, renderer PR #68, transport PR #65. No shared kernel or graphics file edited. Independent review required. Do not merge automatically.

Limitations: no QEMU/game execution here; synthetic positive test data are **not** benchmarks. The validator is not cryptographically authenticated and does not validate the truth of reported metrics, kernel safety, or actual Albion compatibility.
