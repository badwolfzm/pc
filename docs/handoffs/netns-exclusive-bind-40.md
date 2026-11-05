# Handoff: Issue #40 — exclusive loopback bind per namespace

Owner: ChatGPT agent session, 2026-11-05.
Base: `main` at `578a2936d68395cfc3f1104eec5fb2cb4d65bfad`.
Branch: `fix/netns-exclusive-bind-40`.
Paths: `crates/warden-kernel/user/personality.c`,
`tests/compat/net_duplicate_bind.c`,
`.github/workflows/warden-linux-container.yml`, this document.

## Source-reported bug

Before this fix, a stream bound a port only in its own stream record;
the shared uniqueness test inspected `listeners[]`, not bound streams.
Two same-namespace sockets could therefore bind the same port before
either listened, and both subsequently became live listeners. Connections
then selected the last matching listener.

## Behavior after the fix

- `bind()` rejects the same namespace+port when held by any active listener
  or another bound stream, regardless of call ordering.
- `listen()` rechecks the listener table before publishing a listener,
  even if a future change bypasses the bind-time reservation.
- Duplicate binding, connecting, or rebinding the same socket does not
  create an extra reservation. A failed duplicate bind leaves its socket
  allocated but unbound, as it was beforehand.
- Two different network namespaces may still bind and listen on the same
  port; no `SO_REUSEPORT` semantics are introduced.
- Closing streams/listeners releases their reservations. No new global
  state tables or allocations are introduced.

The existing zero-port behavior is not expanded by this change. This
personality server is still a narrow static loopback service, not a real
TCP/IP stack. The underlying server remains shared across the compartments.

## Tests

- `tests/compat/net_duplicate_bind.c`: duplicate bind before listen;
  duplicate bind after listen; namespace-specific reuse; native/Linux
  connection routing; rebind and 256 close/rebind cycles; pool teardown.
- CI: namespace predicate and socket integration tests, new regression,
  Rust unit tests, full kernel/QEMU boot suite.

Status: **awaiting CI result**. Independent review and target Windows/WSL2
test **not verified**. This PR remains draft; never merge automatically.

## Review checklist

Check that stream ownership is kernel-badge-derived, cross-namespace
reservation reuse is allowed, same-namespace reservations cannot alias,
`listen` is atomic under the personality service's single-threaded model,
and all failed paths preserve socket/stream/listener allocation accounting.
