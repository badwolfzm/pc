# Two-Warden-guest evidence verifier

Task: swarm-two-guest-evidence-20261106. Branch: agent/swarm-two-guest-evidence-20261106.
Base: ebdd6826dc3fe5f79b6cdf78f28f555d533a32f2.
Owned paths: tools/swarm-two-guest-evidence/** only.

Run `cd tools/swarm-two-guest-evidence && python3 -m unittest -v`.
Validate `python3 verify.py /path/to/manifest.json`.

Manifest: `{"runs":[{"node_id":"node1","job_id":"job1","vcpu":1,"ram_mib":512,"kernel_sha256":"<64 lowercase hex>","serial_path":"node1.log","serial_sha256":"<64 lowercase hex>","guest_exit_code":0,"elapsed_ms":100},{"node_id":"node2","job_id":"job2","vcpu":1,"ram_mib":512,"kernel_sha256":"<64 lowercase hex>","serial_path":"node2.log","serial_sha256":"<64 lowercase hex>","guest_exit_code":0,"elapsed_ms":100}]}`.

This is **only a schema and local evidence-integrity verifier**. All execution values are supplied by the collector. It does not authenticate the collector, prove real QEMU launches, measure guest RAM, demonstrate communication, attest kernel identity, prove independent execution, or run DOOM. A malicious collector can forge logs and hashes. Use trusted pinned QEMU collection and independent review before making any runtime claim.

The prior standalone prototype was locally exercised with 14 passing tests. The committed GitHub version differs from that prototype and has **not** been executed in this session. Run its included tests in CI before acceptance. No kernel, loader, or other agent-owned paths changed. Request control-thread review and integration; do not merge without the required gates.
