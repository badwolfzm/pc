# Kernel console-capability identity hardening (issue #262)

**Draft security fix, not approved for merge or proven safe on QEMU.**

`Endpoint::with_capacity` defaults to `console: false` for all
user-created IPC endpoints. Only `root_endpoint` (used by reviewed
boot-time console grants) sets that kernel-owned flag to true. Both
`SYS_CONSOLE_WRITE` and `SYS_LOG` now require a live Endpoint+WRITE
capability **and** the flag on the resolved object ID. Minting retains
identity, so authority follows the real object rather than arbitrary slot
number or a supplied badge. Revocation/deletion still operate via cap table,
and a newly created ordinary endpoint defaults to false even if an object ID
is eventually reused.

The single kernel file changed is
`crates/warden-kernel/src/thread.rs`. Its existing syscall numbering,
native ABI, buffer length cap, and user-address-space read remain unchanged.

The Python regression is a **source contract**, not a trusted compiled or
emulated authorization test. CI builds the actual bare-metal kernel target.
Before merge, a security owner must inspect the boot grants and run QEMU
positive/negative tests using ordinary Endpoint+WRITE, revoked caps, wrong
kind/rights and unmapped memory. Make sure all applications that legitimately
use SYS_LOG retain a boot-granted console object, and that the newly denied
ordinary-endpoint path emits zero serial bytes.

The new guard prevents arbitrary IPC endpoints from being console authority,
but it does not make the serial output a secret channel or attest game output.
No Doom/Albion boot, game FPS or whole-guest 512 MiB residency is claimed.
