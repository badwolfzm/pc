# One-core / 512 MiB real Linux HOST resource sampler

The user goal in merged PR #134 is to run ordinary modern PC games in **native independent Warden** on **one vCPU and 512 MiB total guest physical RAM**. Existing PRs #145–#151 (including #148) explore theoretical page-reference traces. This independent tool measures **actual Linux host process** memory and scheduling behavior for a caller-authorized executable; it is NOT another trace simulator.

Usage (Linux):

```sh
CPU=$(python3 -c 'import os; print(min(os.sched_getaffinity(0)))')
python3 tools/ordinary-pc-host-sampler/sample.py --cpu "$CPU" --budget-mib 512 --timeout-sec 30 -- \
  python3 -c 'import time; a=bytearray(32*1024*1024); time.sleep(2)'
python3 -m unittest discover -s tools/ordinary-pc-host-sampler -p 'test_*.py' -v
```

### Measurements and limitations
The script uses Linux `/proc/<pid>/stat` and `/proc/<pid>/status`, not estimates, and sets the child CPU affinity before exec. It reports polled RSS, kernel-reported per-process VmHWM, process thread count and minor/major fault deltas, plus exit/timeout and elapsed wall time. A 512 MiB threshold is **observational only** and is NOT enforced. No actual game is automatically downloaded, installed or launched. Use only binaries you are permitted to run; review your provided command.

**Do not equate Linux process RSS with Warden total guest memory.** Guest kernel, framebuffer, page caches, host/QEMU virtualization, child processes, GPU allocations and memory sharing alter the budget. Linux `/proc` is specific to the host and cannot be used directly by Warden's independent kernel. This tool does not produce raw page-reference traces or frame-time numbers, nor does it establish game compatibility. Short processes may exit before the sampler's first observation and return no evidence.

Requires Linux, Python 3, an allowed logical CPU ID and permission to inspect the launched process. Only the supplied executable is launched; no shell interpretation. Avoid running untrusted binaries. A timeout attempts to kill its process group (the process can spawn descendants that escape the group, so this is not a security sandbox). For security isolation use an actual VM/cgroup sandbox established separately.

Next: compare the observed Linux baseline for freely licensed real game workloads to total-guest measurements from a native Warden QEMU `-smp 1 -m 512M` boot; separately collect page access traces under consent and authorized storage. Coordinate with owners of PR #146 guest evidence and PR #148 page-trace model.

Owned paths: `tools/ordinary-pc-host-sampler/**` and `.github/workflows/ordinary-pc-host-sampler.yml`. Independent review before landing.
