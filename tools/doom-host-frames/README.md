# Genuine Doom in-level host integration: controlled keyboard causality

**Host-side only, not native Warden/QEMU Doom.** The harness compiles the 80
original Doomgeneric upstream translation units at pinned source
`ozkl/doomgeneric@e257a09527062bfb986b6e5e0780839b6cbd5b71`.
It uses the **merged** Warden Doom callback adapter, the bounded authorized
WAD bridge and the upstream WAD class backend, but substitutes explicitly
simulated host syscalls for the Warden kernel and uses host libc.

The workflow downloads GPL Freedoom 0.13.0 from its release and verifies the
**extracted Freedoom Phase 1 IWAD** SHA-256:

```text
7323bcc168c5a45ff10749b339960e98314740a734c30d4b9f3337001f9e703d  DOOM1.WAD
```

No WAD content or upstream GPL engine sources are vendored into this repo.
GPL license obligations still apply to a combined distribution.

## Two independent real-engine process runs

```sh
bash tools/doom-host-frames/run.sh /path/to/pinned/doomgeneric/doomgeneric /path/to/DOOM1.WAD
```

The shell script builds an actual host executable from upstream Doom and
Warden adapter/bridge components, then executes it **twice** with the same
source, game data, synthetic tick progression and 10 post-startup frames.

1. **No-input control**: the mocked keyboard syscall always returns 0.
   Requires genuine `GS_LEVEL`, 10 frames with a non-null player object,
   unchanged player X/Y position and zero sent or consumed arrow transitions.
2. **Input intervention**: injects PS/2 set-1 up-arrow press at gameplay
   frame 2 and release at frame 6. Linker `--wrap=DG_GetKey` captures the
   actual upstream input loop consuming both transitions through the merged
   adapter; requires 10 frames in `GS_LEVEL`, a non-null player and changed
   world position and framebuffer hashes. The unchanged Warden adapter is
   linked in both runs; both processes start independently.

Verified at source commit `319cdb17b3d023a5f96a73ea93b41e6d41f0228a`
in [GitHub Actions run 42264761172](https://github.com/badwolfzm/pc/actions/runs/42264761172):

- No input: player (-27262976,16777216) → (-27262976,16777216)
- Arrow press/release: player (-27262976,16777216) → (-20321275,16779695)
- Both runs: 10 genuine in-level gameplay frames; intervention run:
  one press and release sent and consumed
- This supports a **controlled keyboard→movement inference under this
  deterministic host setup**, not proof about physical keyboard hardware,
  actual guest wall-clock accuracy or arbitrary game behavior.

The merged action-key mappings are checked as a CI precondition:
`KEY_USE=0xa2` for Space and `KEY_FIRE=0xa3` for Ctrl. No obsolete
callback adapter or WAD backend SHA is restored over reviewed main modules.

## Non-claims and native dependencies

Host-rendered frames, fake 10ms ticks and Linux-host libc are **not Warden
kernel execution**. The 10-frame trial is not an FPS benchmark, does not
exercise the one-vCPU 512 MiB QEMU profile, does not account for actual
guest physical frames, and does not demonstrate unmodified Linux/Albion
compatibility. Current genuine native Doom work still requires a complete
freestanding link, reviewed runtime/allocator, larger loader/user virtual
memory mapping, authorized WAD grant at boot, and two real Warden QEMU boots.
Coordinate with native-link audit PR #171, allocator PR #168 and existing
loader/security owners. Review before merging this draft PR.
