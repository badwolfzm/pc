# Real Doom game-loop host smoke (G1 groundwork)

**Not a native Warden execution test.** This probe attempts actual
`doomgeneric_Create()` and `doomgeneric_Tick()` from pinned genuine upstream
Doomgeneric with a permitted IWAD, using the actual Warden platform callbacks
but *host-side mock syscalls* and the bounded WAD bridge. It injects a PS/2 up-arrow key-down and key-up through the genuine Warden
callback, then requires ten presented 320×200 frames with different first and
last framebuffer hashes. A host-link `--wrap=DG_GetKey` interception confirms the **real upstream\nDoom input loop** consumes one press and one release through the unchanged\nWarden adapter. Key injection is simulated, not physical hardware.
The host frame count is NOT an FPS measurement. Frames have hashes but no displayed FPS or native RAM
measurements. A pass proves host-side gameplay/frame-loop initialization, not
freestanding linkage or a QEMU game boot.

CI obtains GPL Freedoom 0.13.0 release game data (phase 1) at an exact release
URL; the WAD is **not stored or redistributed** in Warden. CI still depends
on hosting of a release asset and currently does not embed a cryptographic
release file digest, so production asset provenance must be pinned separately.
Original Doomgeneric GPL obligations remain.

`bash tools/doom-host-frames/run.sh /path/to/pinned/doomgeneric /path/to/DOOM1.WAD`

Depends on unmerged #88 native callback adapter at
`c60e37f85a1f704290234ec95a1aae60622fbb51`, merged WAD class bridge and
upstream WAD ABI shim #114. CI stages files from those exact commits. This
test does *not* claim one-vCPU execution (Linux and the CI host are distinct
from QEMU), security endorsement, an acceptable 2 MiB guest VA window,
low physical RAM, or Albion compatibility. Review security issues in WAD
bridge and kernel before game integration.
