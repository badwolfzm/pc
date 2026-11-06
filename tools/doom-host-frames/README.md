# Real Doom game-loop host smoke (G1 groundwork)

**Not a native Warden execution test.** This probe attempts actual
`doomgeneric_Create()` and `doomgeneric_Tick()` from pinned genuine upstream
Doomgeneric with a permitted IWAD, using the actual Warden platform callbacks
but *host-side mock syscalls* and the bounded WAD bridge. It injects a PS/2 up-arrow key-down and key-up through the genuine Warden
callback, then requires ten presented 320×200 frames with different first and
last framebuffer hashes. A host-link `--wrap=DG_GetKey` interception confirms the **real upstream\nDoom input loop** consumes one press and one release through the unchanged\nWarden adapter. Key injection is simulated, not physical hardware.
The updated fail-closed game-state gate additionally requires genuine Doom's\n`gamestate == GS_LEVEL`, a non-null active player entity on each of the ten\npost-startup frames, and **different initial and final in-level player\nworld coordinates**. A title-screen animation or consumed keys without\nadvancing an actual player can no longer pass this check. The test does not\nisolate causality to the synthetic keypress (other simulation effects can\nmove a player); it checks actual game-state advancement.\n\nThe host frame count is NOT an FPS measurement. Frames have hashes but no displayed FPS or native RAM
measurements. A pass proves host-side gameplay/frame-loop initialization, not
freestanding linkage or a QEMU game boot.

CI obtains GPL Freedoom 0.13.0 release game data (phase 1) at an exact release
URL; the WAD is **not stored or redistributed** in Warden. CI still depends
on hosting of a release asset and currently does not embed a cryptographic
release file digest, so production asset provenance must be pinned separately.
Original Doomgeneric GPL obligations remain.

`bash tools/doom-host-frames/run.sh /path/to/pinned/doomgeneric /path/to/DOOM1.WAD`

Uses the **merged** Warden Doom callback adapter (#88, including
Space/Ctrl action-key fixes #111), merged bounded WAD class bridge, and
merged WAD file ABI backend from the checkout. CI no longer stages obsolete
pre-merge callback or file-backend commits, and explicitly checks corrected
`KEY_USE=0xa2` / `KEY_FIRE=0xa3` mappings before host gameplay. This gate
was confirmed in [GitHub Actions run 42260455630](https://github.com/badwolfzm/pc/actions/runs/42260455630).
Genuine Doom was in `GS_LEVEL` for all 10 post-startup frames; its player
entity moved and the real Doom input poll consumed the simulated arrow-key
press/release. This
test does *not* claim one-vCPU execution (Linux and the CI host are distinct
from QEMU), security endorsement, an acceptable 2 MiB guest VA window,
low physical RAM, or Albion compatibility. Review security issues in WAD
bridge and kernel before game integration.
