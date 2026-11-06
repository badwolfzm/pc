# Swarm QEMU argv guard

A small fail-closed host-side preflight for **operator-pinned** QEMU commands.
The operator must independently protect the policy file, binary, kernel image,
VM disks and launch service. Never accept an approved policy from a remote job.

Run tests:

```sh
cd tools/swarm-qemu-argv-guard
python3 -m unittest -v test_guard.py
```

The command-line interface reads JSON files with `approved_argv` and
`candidate_argv` arrays, respectively. It exits 0 only on acceptance.
Use `python3 guard.py --policy operator.json --candidate request.json`.

This validates *argument equality*, one `-smp 1` and `-m 512M`, and
rejects a small list of host-control flags. It is not a comprehensive QEMU
sandbox: an operator-approved dangerous `-device`, `-netdev`, disk,
network, display or accelerator setting could still be unsafe. A production
controller should use a reviewed narrow device/network whitelist, pinned
artifact digests, OS-level sandboxing, authenticated transport, quotas and
timeouts. This tool does not launch QEMU or inspect a running guest.

**Unverified:** guest-to-guest networking, GPU isolation, effective memory
accounting, QEMU execution, Doom frames and distributed workloads.
