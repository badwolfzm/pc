# Read-only capability-scoped guest filesystem: v1

Issue #29 partial prerequisite for broad Linux software compatibility.

This PR introduces a separate, allocation-free userspace lookup component
rather than modifying the active personality server under PR #24. It has
**no host filesystem calls**. The trusted server supplies an immutable
list of canonical absolute files and a fixed read grant rooted at a
canonical directory. A Linux guest must never choose or mutate its grant.

## Contract

- Accept only nonempty canonical absolute paths no longer than 255 bytes.
  Refuse embedded NUL/control bytes, backslashes, repeated separators,
  trailing slash, and `.` / `..` path components.
- A grant for `/site` exposes `/site` and `/site/...` only, never
  `/siteevil`, `/internal`, or paths outside the root. A grant for `/`
  covers the canonical absolute tree.
- Reject access outside the grant **before** file enumeration, so callers
  do not learn whether a foreign path exists.
- Return only immutable file references, and copy bounded ranges with
  subtraction-based offset checks that cannot overflow.
- Lookup and reads require no allocation or syscalls; the table is fixed
  by the service and capped at 1024 entries.

## Security limitations

This is **not yet integrated** into the Warden personality server. It is not
a complete POSIX filesystem: no directory enumeration, metadata/stat,
file descriptor lifetime, write operations, mount namespace, file-backed
mmap, symlinks, hardlinks, or actual host disk access. No Warden kernel
security guarantee is implied by passing the unit tests. Future
integration must verify grant provenance, badge-to-client mapping, per-client
resource budgets and thread teardown; paths cannot be treated as authority.

## Acceptance and handoff

- Claimed issue: #29; branch `feat/capability-rofs-v1` from `main`.
- Files owned: `crates/warden-kernel/user/rofs.[ch]`, `tests/rofs/`,
  `.github/workflows/warden-rofs.yml`, this handoff.
- Existing claimed `personality.c`, kernel ABI, ELF loader, agent
  coordination and control files are intentionally untouched.
- Test command:

```sh
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all \
    crates/warden-kernel/user/rofs.c tests/rofs/test_rofs.c -o /tmp/warden-rofs-test
/tmp/warden-rofs-test
```

- CI verified on PR head: [GitHub Actions run 42010285491](https://github.com/badwolfzm/pc/actions/runs/42010285491): **733 assertions passed under GCC + UBSan and 733 under Clang**. This validates only the standalone C service, not its integration into Warden.
- Existing kernel regression workflow also passed on the PR branch: [run 42010285920](https://github.com/badwolfzm/pc/actions/runs/42010285920), Warden/QEMU **183/183 checks**. Those checks do not invoke this standalone ROFS module.
- Still required: independent review, integration with the existing
  personality server by its owner, kernel/QEMU syscall-to-rofs tests,
  evaluation on the target Windows/WSL2 host.
