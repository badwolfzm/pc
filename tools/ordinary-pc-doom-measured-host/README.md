# Actual Freedoom Linux host baseline: one CPU + RAM measurement

After merged PR #134 (ordinary modern-PC games on one vCPU and **512 MiB total Warden guest RAM**), merged PR #131 (genuine pinned upstream Doom with Freedoom assets), and draft PR #156 (actual Linux host RSS/fault sampler), this independent test **combines** their work. Unlike the many synthetic page-trace PRs, it executes a genuine game engine with genuine freely licensed game assets, pins it to **one logical host CPU**, and collects kernel-reported host process metrics.

## Reproduce

```sh
# Required: genuine source at exact ozkl/doomgeneric commit e257a09527062bfb986b6e5e0780839b6cbd5b71
#            original Freedoom 0.13.0 phase-1 freedoom1.wad (hash checked).
python3 tools/ordinary-pc-doom-measured-host/measure.py \
  --source /path/to/doomgeneric/doomgeneric --wad /path/to/freedoom1.wad
```

For CI, the workflow fetches exact pinned upstream Doomgeneric and release Freedoom data, and **stages the sampler from its exact PR #156 commit SHA without changing that branch**. It rejects a WAD whose bytes do not match SHA-256 `7323bcc168c5a45ff10749b339960e98314740a734c30d4b9f3337001f9e703d`. It compiles 80 genuine engine translation units, links original host backend, calls `doomgeneric_Create` and `doomgeneric_Tick`, and requires successful game exit plus at least one observed process sample. The game's own smoke asserts at least 10 framebuffer presentations (previous baseline delivered 60).

Log event `HOST_GAME_MEASUREMENT=` contains real host RSS and VmHWM, sampled process threads, fault delta, elapsed time, exit code, CPU affinity, the game WAD hash, and explicit host-only limitations. GPU allocations and child processes are not counted. A 512 MiB *observational* threshold never enforces a hard guest memory limit. Timing includes intentional sleeps; this is **not a valid maximum FPS benchmark**, just a real engine workload baseline. No proprietary game/asset modifications or bypasses.

## What's not proven

This is **Linux-host execution using glibc and host stdio**, not Warden's independent kernel. It does **not** prove games can run inside Warden, that 512 MiB entire guest RAM suffices, that Albion Online can run, or that host process RSS equals guest total RAM. Next integration: Warden-native single-vCPU QEMU with actual 512 MiB physical cap, authorized WAD storage, framebuffer input and kernel+game+metadata memory telemetry. CPU-affinity constrains game threads inherited from process creation only, not the compiler or independent host helpers.

Paths exclusively owned: `tools/ordinary-pc-doom-measured-host/**` and `.github/workflows/ordinary-pc-doom-measured-host.yml`. Depends on merged engine test and #156 sampler; avoid touching their files. Review and approve separately; no auto merge.
