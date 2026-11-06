# Warden swarm QEMU argument guard (prototype)

This is a **lint helper**, not a QEMU security sandbox, a guest memory monitor,
a GPU isolation mechanism, or a working multi-node Warden network.

Run from repository root:

```sh
python3 -m unittest discover -s tools/swarm-qemu-budget -p 'test_*.py' -v
python3 tools/swarm-qemu-budget/check.py 'qemu-system-x86_64 -smp 1 -m 512M -machine q35 -kernel target/warden-kernel.mb32 -nographic'
```

The checker does **not** execute the supplied command. The allowlist rejects
unsupported options rather than guessing their semantics. In particular, it
does not support memory backends, NUMA, arbitrary devices, response files,
external config files, or network configuration. Passing the checker does not
prove guest memory accounting, the number of graphics devices, network
isolation, or any Warden guest behavior.

## Next milestones

1. Obtain a reviewed QEMU launcher with explicit one-vCPU, RAM and one
   graphics-device configuration. Do not accept arbitrary QEMU arguments.
2. Boot two *real* Warden instances, each within the configured budget, and
   capture boot logs, exact kernel commit and QEMU version.
3. Add an authenticated network-fabric transport with identity binding,
   replay protection, bounded messages and revocation; test failed handshakes.
4. Prove two nodes exchange a nonce-bearing request and verified response,
   including disconnect/reconnect and isolation tests.
5. Scale the same protocol to four and eight nodes, then dynamic enrollment.
6. Separately run genuine Doom within a guest and record frame correctness,
   input and frame timing. Host-side simulation is not acceptance.

No multi-guest execution or network-fabric tests are claimed here.
