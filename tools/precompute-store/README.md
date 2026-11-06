# S1 host reference: precomputed artifact store

Task [#85](https://github.com/badwolfzm/pc/issues/85), architecture [#76](https://github.com/badwolfzm/pc/issues/76), draft vision [PR #79](https://github.com/badwolfzm/pc/pull/79).

This **host-only Python stdlib prototype** stores immutable SHA-256 addressed precomputed objects into an explicitly approved *dedicated directory*, with namespaces, per-object and per-namespace byte limits, atomic hard-link publication, integrity verification on every read, and deterministic metadata manifests. It does **not** mount, discover, format, partition or access arbitrary devices: an authorized directory may reside on a suitably mounted device, but the responsible platform must grant it first.

Run tests: `python3 -m unittest discover -s tools/precompute-store -p 'test_*.py' -v`.

Example (host only; isolated writable directory you own):

```python
from store import Store
with Store("/path/to/previously-approved-dedicated-folder", "doom") as store:
    payload = store.put(b"deterministic precomputed bytes")
    manifest = store.put_manifest("texture", {"source": payload}, payload, "build_v1")
    assert store.get(payload) == b"deterministic precomputed bytes"
```

**Boundaries and risks:** Root directory and its parent ownership must already be trusted. The prototype uses directory-relative operations with `O_NOFOLLOW`, object hashing and restrictive new-file permissions on POSIX/Linux. It is not an audited multi-user or cross-process-safe quota system: concurrent writers can race on quota, malicious host writers with access to the same namespace can deny service, and persistency relies on underlying filesystem semantics. Do **not** run it against untrusted network filesystems, existing personal directories, or privileged device nodes; only use a newly provisioned exclusive directory on an appropriate filesystem. It does not implement namespace ACLs, encryption, resource revocation after an open, signature verification, transaction journaling, storage hardware enumeration, a paging/swap backend or Warden capability enforcement. File names are validated to prevent traversal; hard-linked objects are refused on read. No confidential inputs should be stored without an encryption and access-control design.

**Handoff to storage and kernel owners:** Replace host path authorization with Warden service capability handles; add authenticated grants, strict lifetime/revocation, trust-root and quota enforcement, safe concurrent accounting and durable commits; integrate the existing `matrixos/assets/` read-only chunk format by a separate reviewed adapter. Make page-backed swap separately under PR #82; **do not treat these cache objects as memory swap slots**. Compare cold/warm loading with bounded RAM before asserting performance. No integration with actual Doom, Albion, or a Warden VM is claimed.

Ownership: `tools/precompute-store/**` plus dedicated CI workflow only. Respect independent game PR #80, swap PR #82, graphics PR #58 and asset owner. Keep draft for independent security review and coordination handoff.
