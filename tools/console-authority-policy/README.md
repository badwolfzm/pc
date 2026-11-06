# Kernel handoff: native console must authorize the designated object, not any endpoint

Issue [#269](https://github.com/badwolfzm/pc/issues/269). Current `crates/warden-kernel/src/thread.rs` handles `SYS_CONSOLE_WRITE=13` by checking only `Rights::WRITE` and `ObjectKind::Endpoint` before calling `serial::write_bytes`. An unrelated writable IPC endpoint therefore passes this branch. The existing `SYS_HOST_EXIT` handler correctly additionally checks the exact machine-control object identity.

**Required kernel-owner implementation:** store or derive nonforgeable authoritative console object identity in kernel state and check it together with Endpoint+WRITE before any user buffer read or serial output. Never trust a guest supplied object ID as authority. Reject if console is missing, revoked, wrong kind, read-only or unrelated; preserve 1024-byte bound and unmapped pointer rejection. Add real-kernel negative tests with a regular independently created Endpoint+WRITE and real designated console rights. Do not broaden general endpoint rights. Any dedicated console permission/ABI changes need lead and rights contract approval.

This PR provides only an independent Rust **pure reference predicate** and five unit tests, plus a read-only diagnostic that explicitly reports today's missing identity check. It does not change the kernel, protect the running syscall, or establish Doom gameplay/512 MiB performance; the kernel owner must wire the logic and test actual dispatch before calling #258 security-ready.

```sh
rustc --edition=2021 --test tools/console-authority-policy/policy.rs -o /tmp/console-policy-tests
/tmp/console-policy-tests
python3 tools/console-authority-policy/check_kernel.py
```

Owned only NEW `tools/console-authority-policy/**` and its dedicated workflow; shared kernel edits are not authorized under the agent coordination plan. Independent kernel/security review required.
