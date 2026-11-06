# Secure optimization-plan boundary for MatrixOS and FastStripCC research

This opt-in **plan-only** frontend lets users select current MatrixOS `mxcc` profiles (`matrixos`, `runtime-fast`, fixed x86 CPU tiers) for *trusted local C source* without invoking a compiler or loading any executable.

```sh
python3 tools/warden-secure-compiler/secure_profiles.py \
  --trusted-source-root /path/to/reviewed/source \
  --source hello.c --profile runtime-fast --tier baseline
python3 -m unittest discover -s tools/warden-secure-compiler -p 'test_*.py' -v
```

The `size` experiment currently maps to MatrixOS's existing function/data-section + linker-GC profile; it is **not** a real FastStripCC optimization or a guaranteed smaller binary. Selection of an actual `faststripcc` compiler is **refused**, because the Library-referenced FastStripCC source has not been installed and security-reviewed in this repository. Prior FastStripCC benchmark reports showed a wrapper around GCC/Clang, not a verified arbitrary-binary optimizer.

**No execution privileges are granted.** The program emits bounded JSON metadata and SHA-256 of one nonempty regular C source (maximum 2 MiB) beneath a trusted root. It never launches a shell, compiler, subprocess, game or user script. No user-selected extra compiler flags, executable paths, linker scripts, compiler plugins, build scripts, environment overrides or output locations are accepted. Path traversal, absolute paths, option-like path segments and symlinks are refused. The source directory must itself be controlled by a trusted owner and **not writable by an untrusted client**.

SHA hashing with O_NOFOLLOW/dev/inode/size checks **does not establish a race-free hostile filesystem snapshot**. A subsequent build must pin immutable content and verify SHA again. Emitting a plan does not establish source authenticity or server-side sandbox safety. The live Warden game server must **never compile, link or run untrusted uploads** based on this metadata.

Actual third-party code compilation requires a *separately reviewed, dedicated offline worker/VM*, non-root account, no network/credentials, mount and syscall isolation, per-job CPU/memory/storage/time limits, immutable reviewed toolchain, content provenance, output scanning, reproducibility checks and an explicit artifact approval process before any runtime use. A Linux compiler, subprocess limits, or a Python wrapper alone are **not** a security sandbox. Never modify protected game executables or circumvent anti-cheat.

Task ID: `secure-compiler-matrixos-faststrip-20261106`. Scope: new `tools/warden-secure-compiler/**` and dedicated CI workflow only. Base main `b8a399665eb6f929d4fcc83cc9679f831ea7aae2`. The existing compiler `matrixos/frontend/**`, sandbox/kernel, and other owners' modules remain untouched. Validation: 12 Python unit tests plus CI, documentation; no native Warden or Albion performance claim. Requires independent security review before any consumer execution integration.
