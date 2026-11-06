# Real Doom engine + genuine free Freedoom IWAD (host proof)

This integration gate downloads an exact release of the freely licensed [Freedoom](https://freedoom.github.io/) Phase 1 IWAD as CI test data. It compiles 80 genuine Doomgeneric C engine modules pinned to `ozkl/doomgeneric@e257a09527062bfb986b6e5e0780839b6cbd5b71`, links the ORIGINAL upstream stdio WAD backend and a minimal headless host display, calls **actual** `doomgeneric_Create()` followed by `doomgeneric_Tick()`, and requires at least 10 rendered frames. The fixture is not copied into the Warden repository. The workflow prints SHA-256 and file size.

When successful this demonstrates Doom + real Freedoom data + engine initialization + gameplay loop under **Linux host libc**, with framebuffer callbacks. It does **not** exercise Warden's storage capabilities, native display ABI, native guest boot, or bounded guest RAM. It is not the final single-vCPU native gameplay acceptance. Do not interpret source compilation or a static link alone as proof that the engine ran.

The integration's eventual goal is to replace host stdio with the reviewed Warden WAD bridge and run precisely this engine startup/frame acceptance inside the Warden guest. Owners must first provide authorized guest WAD storage, a sufficient memory window, a freestanding runtime and allocator, and the platform callback boundary. No commercial WAD, Doom engine fork or upstream file is vendored.

Local reproducibility: provide pinned Doomgeneric source checkout and a lawful Freedoom Phase 1 WAD, then `bash tools/doom-real-freedoom-host/run.sh <doomgeneric-dir> <freedoom1.wad>`. CI fetches Freedoom v0.13.0 as the test asset. No assertion of cryptographic provenance unless the downloaded hash is independently pinned and verified.

Owned new paths only: `tools/doom-real-freedoom-host/**` and dedicated workflow. PR #124 host link does not start the engine; this test adds actual asset-backed engine startup.
