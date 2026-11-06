# PR #134 follow-on: 512 MiB offline working-set research analyzer

Standalone **host Python** tool for authorized page-reference JSONL traces, using
an LRU model and a total guest budget of **512 MiB**. The default 64 MiB reserve
for kernel, graphics, runtime and swap metadata is an **assumption**, not a
measurement. It uses no kernel code, game binaries, telemetry hooks or devices.

One JSON object per line, with monotonically nondecreasing timestamps in µs:

```json
{"t_us":0,"page":42,"write":false}
{"t_us":15000,"page":50,"write":true}
{"t_us":17000,"page":42,"write":false}
```

```sh
python3 tools/ordinary-pc-trace-512mb/analyze.py trace.jsonl --guest-ram-mib 512 --reserve-mib 64
python3 -m unittest discover -s tools/ordinary-pc-trace-512mb -p 'test_*.py' -v
```

Reports unique hot pages in trailing 16 ms, 100 ms and 1 s windows; modeled LRU
hits/faults, clean/dirty evictions and minimum modeled page-in/out bytes. Rejects
malformed or out-of-order traces, inputs over 128 MiB or two million events,
invalid page IDs, or budgets exceeding 512 MiB.

Window distinct pages above capacity signal **locality pressure**. They are not
proof a game is impossible because those pages could be streamed within the
interval. No actual Warden page-fault behavior, compression overhead, latency,
CPU/GPU use, FPS, physical-RAM enforcement or game compatibility is measured.
Do not upload sensitive raw virtual addresses; renumber page IDs consistently.

Owned paths: `tools/ordinary-pc-trace-512mb/**` and its dedicated workflow
only. Does not edit #134's merged vision, kernel, loader, or other agents'
storage, paging or game sources. Independent review required.
