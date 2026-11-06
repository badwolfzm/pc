# Ordinary PC games — one vCPU / 512 MiB evidence envelope

This is the first **executable validation contract** supporting the owner target in
[PR #134](https://github.com/badwolfzm/pc/pull/134). It is **not** a pager,
CPU limiter, game runtime, benchmark, or trustworthy telemetry collector.

Use:

```bash
python3 -m unittest discover -s tools/ordinary512-envelope -p 'test_*.py' -v
python3 tools/ordinary512-envelope/check.py path/to/report.json
```

The report is bounded to 1 MiB UTF-8 JSON, schema `ordinary512-v1`.
It must identify `platform: warden-qemu`, one guest vCPU, **exactly**
536,870,912 bytes of configured total guest physical RAM and a declared
`cpu-software` or `integrated-gpu` rendering profile. The profiles are
**not** equivalent and must not be combined when making performance claims.

`provenance` contains exact Warden commit SHA-1, game SHA-256, source
description, and `telemetry_kind` explicitly either `synthetic-fixture`
or `guest-instrumented`. Synthetic fixtures exist only for checking tool
behavior; **they are never evidence of successful game runs**.

Each time-ordered `samples[]` item has `t_ms`; *cumulative* counters
`swap_read_bytes`, `swap_written_bytes`, `page_faults`; and seven
**mutually exclusive resident physical byte** buckets:
`kernel`, `runtime`, `game`, `graphics`, `page_cache`,
`swap_metadata`, and `other`. All buckets are required, nonnegative
integers. Total is summed for each sample and must stay <= 512 MiB.
A sample with 512 MiB exactly passes; 512 MiB + one byte fails.
**Never double count an overlapping physical frame**, even if it is mapped
to more than one virtual address or referenced by two owners. An upstream
telemetry producer must establish exclusive classification and correct
physical frame accounting; this checker cannot detect intentional undercount.

`authorized_swap_bytes` reports provisioned lawful storage capacity but is
NOT extra RAM. Swap pages currently resident in RAM, compressed data,
staging/decompression buffers, graphics framebuffers and kernel page tables
are charged to the appropriate resident buckets. Host GPU memory and the
host/QEMU resident set are separate facts to measure; neither is silently
treated as free memory. Physical guest RAM of exactly 512 MiB may have fewer
usable pages after reserved areas. The checker assumes no deliberate
overcommit of resident physical memory.

Example minimal **synthetic-only** report:

```json
{
  "schema":"ordinary512-v1","platform":"warden-qemu",
  "configuration":{"vcpus":1,"guest_ram_bytes":536870912,
    "graphics_backend":"cpu-software","authorized_swap_bytes":0},
  "provenance":{"commit":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
    "game_sha256":"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
    "source":"synthetic fixture only","telemetry_kind":"synthetic-fixture"},
  "samples":[{"t_ms":0,
    "resident":{"kernel":1048576,"runtime":0,"game":0,"graphics":0,
      "page_cache":0,"swap_metadata":0,"other":0},
    "swap_read_bytes":0,"swap_written_bytes":0,"page_faults":0}]
}
```

A passing report only verifies internal structure, cap and arithmetic.
A genuine acceptance needs an independently checked native Warden/QEMU
boot on one vCPU, measurement of **full** physical memory and privileged
kernel consumers, comparison with the ordinary-PC baseline, authorized
storage and security-negative tests, keyboard/game-state advancement,
separate CPU vs integrated-GPU results, and real frame timestamps.
No storage-first method can eliminate CPU work, network deadlines or
hardware API compatibility constraints. No promise that every game is
feasible on 512 MiB.

Ownership: new path `tools/ordinary512-envelope/**` only; separate from
G0 `tools/game-budget-gate/**`, host envelope, pager work and kernel memory
accounting. Linkage to PR #134 is a review handoff, not an implied merge.
