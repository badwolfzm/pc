Task: #66 | owner ChatGPT session | base b2eb17f1c5c0e57a57f6c672c793cc3638d0c2a9
Owned: tools/game-frame-probe/**, .github/workflows/game-frame-probe.yml
Scope: Host-only deterministic software column renderer and CI; no Warden guest integration, no game claims.
Dependency: #62 / draft PR #63 game gates; PR #58 graphics and #61 reliable kernel IPC.
Validation planned: Linux GitHub Actions GCC strict build, Python deterministic smoke, UBSan. Local Warden/QEMU and PC: NOT RUN.
Reviewer: independent graphics and security review before any incorporation into guest.
Do not modify/claim shared kernel, MatrixOS graphics or roadmap owner's paths.
