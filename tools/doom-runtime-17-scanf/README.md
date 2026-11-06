# Actual Doom 17-import native runtime composition

Task #267. This **does not** introduce another C runtime provider or change the kernel. It combines the previously merged 16-provider genuine Doom engine ET_REL bundle with the exact pinned scanner from draft PR #247, commit `62dc9f73bee7753bd81d889a0e44ee8e95ce2220`. It checks the actual merged Doom engine object and the scanner's exported/imported symbols using `nm`, rejects duplicates, and uses `ld -r` to verify exactly `__isoc99_sscanf` disappears and **no other imports disappear or appear**.

The dedicated workflow fetches original upstream `ozkl/doomgeneric@e257a09527062bfb986b6e5e0780839b6cbd5b71`, stages the canonical merged formatter at `4246bde3e786f1c51e3b047292a592cb5c057bf1`, stages PR #247 scanner source from its exact commit, checks exact provenance, runs the existing genuine 16-provider composition, and checks the 17th symbol delta. It does not merge or modify either source provider.

No executable or native Warden boot exists as a result of this tool. In particular, `ld -r` permits unresolved host console/file/process APIs. No one-vCPU runtime, 512 MiB guest-memory measurement, FPS, or Albion Online compatibility is implied.

Paths exclusively owned: `tools/doom-runtime-17-scanf/**`, `.github/workflows/doom-runtime-17-scanf.yml`. Review before merge; if the scanner branch changes or PR is integrated, reconcile and remove the now-redundant pinned staging technique. Security and ABI assumptions require independent review.
