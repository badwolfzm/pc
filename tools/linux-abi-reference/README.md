# Native Linux ABI reference probes

This tool supplies reproducible **host Linux x86-64 reference behavior** for
the negative-case requests in [PR #26](https://github.com/badwolfzm/pc/pull/26)
and [review #32](https://github.com/badwolfzm/pc/issues/32).
It does not execute inside Warden, validate Warden's pointer helpers, or
establish Linux application compatibility. It has no dependency on the
pending network/ABI/ELF-tool PRs and changes none of their files.

## Run

Requires Linux x86-64, a C compiler and Python 3. Use from the repository root:

```sh
python3 tools/linux-abi-reference/test_probe.py
```

The three tests compile with `-std=c11 -O2 -Wall -Wextra -Werror` and execute
the real native syscalls. The second deliberately changes the expected
missing-PID error in a temporary source copy, then requires a nonzero process
exit and failed JSON cases. The third corrupts a sentinel in a temporary
copy to require consistent per-case failure and summary counts. A missing compiler or restricted host is a test
failure, not a fabricated pass. `CC` can select a compiler command.

To capture the unmodified probe's JSON without leaving build files in the repo:

```sh
cc -std=c11 -O2 -Wall -Wextra -Werror tools/linux-abi-reference/probe.c -o /tmp/warden-abi-reference
/tmp/warden-abi-reference > /tmp/warden-abi-reference.json
python3 -m json.tool /tmp/warden-abi-reference.json
```

Exit 0 means the native reference checks passed, 1 means a behavior mismatch,
and 2 means setup failed. An interrupted or setup-failed run can have incomplete
JSON; check the process status before accepting its output. Every complete
report labels its execution `native-linux-reference`.

## Cases and interpretation

| Family | Cases |
|---|---|
| `sched_getaffinity` | Self, nonexistent PID, zero size, unaligned size, inaccessible destination, read-only destination |
| `getrlimit` | Valid query, invalid resource, inaccessible/read-only destination, output crossing a protected page |
| `prlimit64` | Self query and nonexistent PID; no limits are changed |
| `clock_getres` | Invalid clock ID, supported clock with NULL output, read-only output |

The probe uses raw `syscall` entry through libc. Thus syscall failure is
reported as result `-1` with a positive `errno`, unlike Warden's guest ABI's
negative errno return. Affinity mask length, allowed CPU bits and resource
limit values depend on the host: do not copy those numbers into Warden's
virtual single-CPU expectations. The report's `output_unchanged` is a boolean
only for the affinity cases whose valid destination has a sentinel; otherwise
it is `null` (not checked). Successful output values are not certified by this
negative-case tool.

Protected pages come from private anonymous mappings in this process. The
probe never guesses a kernel pointer, changes resource limits, creates tasks,
reads files, contacts a server, or signals another process. Its nonexistent
PID is `INT_MAX`, beyond Linux's PID limit. A sandbox's own syscall restrictions
can change results; record that as an environment limitation, not a kernel bug.

Linux may partially write an output before returning `EFAULT` for a cross-page
copy. Only the errno is asserted for that case; this is **not** a requirement
to weaken Warden's existing whole-range prevalidation/unchanged-on-refusal
contract. Keep stronger Warden-specific checks as well.

## Follow-up for the Warden owner

Linux's affinity PID lookup and word-alignment checks are defined in
[kernel/sched/core.c](https://code.googlesource.com/linux/torvalds/linux/+/26ba1bf310f0ed43f249a93d0cf8a93675cd8ae8/kernel/sched/core.c).

Port applicable cases into actual QEMU guest checks, retaining native Warden
and Linux-personality test distinctions. Declare self/foreign/nonexistent PID
policy and unsupported inputs explicitly. Validate read-only, unmapped and
cross-page outputs against Warden's actual address spaces. This host tool is
a reference for that work, not a substitute for it or the target-PC gate.
