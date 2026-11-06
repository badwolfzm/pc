# Two-Warden-guest evidence verifier

Task: swarm-two-guest-evidence-20261106. Branch: agent/swarm-two-guest-evidence-20261106.
Base: ebdd6826dc3fe5f79b6cdf78f28f555d533a32f2.
Owned paths: tools/swarm-two-guest-evidence/** only.

Run `cd tools/swarm-two-guest-evidence && python3 -m unittest -v`.
Validate `python3 verify.py /path/to/manifest.json`.

Manifest: `{"runs":[{"node_id":"node1","job_id":"job1","vcpu":1,"ram_mib":512,"kernel_sha256":"<64 lowercase hex>","serial_path":"node1.log","serial_sha256":"<64 lowercase hex>","guest_exit_code":0,"elapsed_ms":100},{"node_id":"node2","job_id":"job2","vcpu":1,"ram_mib":512,"kernel_sha256":"<64 lowercase hex>","serial_path":"node2.log","serial_sha256":"<64 lowercase hex>","guest_exit_code":0,"elapsed_ms":100}]}`.

This is **only a schema and local evidence-integrity verifier**. All execution values are supplied by the collector. It does not authenticate the collector, prove real QEMU launches, measure guest RAM, demonstrate communication, attest kernel identity, prove independent execution, or run DOOM. A malicious collector can forge logs and hashes. Use trusted pinned QEMU collection and independent review before making any runtime claim.

The prior standalone prototype was locally exercised with 14 passing tests. The committed GitHub version differs from that prototype and has **not** been executed in this session. Run its included tests in CI before acceptance. No kernel, loader, or other agent-owned paths changed. Request control-thread review and integration; do not merge without the required gates.

## Verification update (2026-11-06)

The parser now rejects duplicate JSON object keys (including nested keys), preventing an ambiguous manifest from being silently accepted. The committed regression suite has 16 cases; a corresponding local reconstruction was executed with `cd /mnt/data/warden-github-test && python3 -m unittest -v` and reported 16 passed in Python 3.13. The GitHub branch's suite has not been fetched and rerun byte-for-byte by this session. Dedicated workflow `.github/workflows/warden-swarm-two-guest-evidence.yml` was added, but no successful GitHub Actions run is claimed. No QEMU guest was launched.

Security limitations: a collector can lie about CPU/memory/exit and generate internally consistent hashes; file paths may change between validation and open (TOCTOU). The verifier cannot authenticate a host or attest guest identity. Integration must use trusted, pinned collection, isolated artifacts and actual guest-side proofs.
