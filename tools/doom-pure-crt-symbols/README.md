# Two non-overlapping pure runtime symbols for genuine native Doom

Issue #174 and PR #177. Original PR included six pure functions but four
string operations are already independently owned by PR #175. This branch
now implements **only abs and fabs**, avoiding duplicate symbols:
`abs` is ISO C and has undefined behavior for INT_MIN; `fabs` clears the
binary64 sign bit and preserves NaN payload.

No heap, syscalls, file/network calls, terminal I/O, subprocess execution,
capability or Warden kernel changes. CI compiles and UBSan-tests two
pure routines, checks `nm -u` of the freestanding object is empty, and
relinks against genuine pinned Doom engine and Warden adapters from #170.
The expected result is **exactly two** genuine undefined symbols removed,
and security-sensitive external calls remaining unresolved.

This is ET_REL, not an executable and not a Warden native Doom boot.
Integrator should include the reviewed string extras from #175 separately,
then resolve the remaining allocator, I/O, formatting, WAD and loader
services with explicit security ownership before attempting a guest.
