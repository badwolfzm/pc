# Real Doom: 300 individually timed and hashed frames (Linux HOST)

This is a follow-up to merged PR #134 (ordinary modern PC games, one vCPU/512 MiB total guest budget), merged #131 (actual Doom engine+Freedoom 60 host callbacks), merged #154 (real native Warden **Pong** at one vCPU/512 MiB), and draft #159 (Doom host RSS baseline).

CI fetches **unaltered** `ozkl/doomgeneric@e257a09527062bfb986b6e5e0780839b6cbd5b71` and Freedoom 0.13.0 Phase 1 asset (SHA-256 `7323bcc168c5a45ff10749b339960e98314740a734c30d4b9f3337001f9e703d`). It uses the existing inventory tool to compile **80** genuine upstream C units, links a small host-only callback implementation, runs `doomgeneric_Create()` and `doomgeneric_Tick()` on **one allowed Linux CPU** and saves 300 actual framebuffer callback timestamps and full 320×200 pixel hashes. A separate verifier rejects missing/duplicate/reordered frames, a stalled timestamp or a constant image. It computes observed *paced callback rate*, and p50/p95/p99 frame intervals, not uncapped FPS.

```sh
python3 -m unittest discover -s tools/doom-300-frame-host-evidence -p 'test_*.py' -v
bash tools/doom-300-frame-host-evidence/run.sh /path/to/pinned/doomgeneric \
    /path/to/freedoom1.wad /tmp/doom-frame-proof
```

Outputs `doom-frames.log` and `frame-evidence.json` are uploaded as CI artifacts if the job runs; the test fails closed on a short/incorrect frame sequence. No copyrighted id Software game assets are bundled; the original free Freedoom asset is retrieved only during CI.

**Limits:** this is still the Linux host, using host C runtime and file IO. One CPU **affinity** is not a Warden guest, and an intentional 28 ms sleep sets a ~35 Hz target; neither observed callback rate nor full-frame hashes prove native Doom inside Warden, actual maximum game FPS or 512 MiB total guest footprint. Static camera animation may produce multiple hashes; true player interaction is not yet validated. The 512 MiB resource profile remains a native Warden/QEMU requirement, not a host RSS claim.

Owner task `doom-300-frame-host-evidence-20261106`; owned new paths `tools/doom-300-frame-host-evidence/**` and one workflow only. Dependencies: merged #131, upstream engine, merged guest Pong profile #154; #159 separately handles host process RSS. No overlap with kernel, frame backend, swap, working-set or other live agent PRs. Independent review required.
