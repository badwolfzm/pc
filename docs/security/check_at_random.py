#!/usr/bin/env python3
"""Source-level smoke check for Warden Linux AT_RANDOM (issue #36).

Usage:
    python3 docs/security/check_at_random.py crates/warden-kernel/src/linux.rs
    python3 docs/security/check_at_random.py --self-test

WARNING: passing a source scan cannot prove cryptographic strength.
A real Warden/QEMU test and independently reviewed entropy provider are required.
"""
import argparse
from pathlib import Path
import re
import unittest


def observed_seed(ticks, argv0_length):
    """Reproduce the exact 16-byte expression from current main."""
    return bytes(((ticks & 255) * 31 + i * 7 + argv0_length) & 255 for i in range(16))


def assess(source):
    start = source.find("fn init_stack(")
    end = source.find("// ---- dispatch", start) if start >= 0 else -1
    if start < 0 or end < 0:
        return ["UNKNOWN: cannot locate initial-stack implementation; manual review needed"]
    stack = source[start:end]
    findings = []
    if "(25, random_at)" in stack and re.search(r"ticks\s+as\s+u8", stack) and "wrapping_mul(31)" in stack:
        findings.append("FAIL: AT_RANDOM publishes tick-derived predictable bytes (issue #36)")
    elif "(25, random_at)" in stack:
        findings.append("UNKNOWN: AT_RANDOM provider provenance requires manual review")
    else:
        findings.append("UNKNOWN: missing or changed AT_RANDOM encoding requires review")
    pos = source.find("fn getrandom(")
    if pos >= 0:
        nxt = source.find("\nfn ", pos + 4)
        body = source[pos:nxt if nxt >= 0 else len(source)]
        if "x ^= x >> 12" in body and "x ^= x << 25" in body:
            findings.append("FAIL: getrandom returns xorshift64* output")
    return findings


class KnownImplementationTests(unittest.TestCase):
    def test_seed_repeats_every_256_ticks(self):
        self.assertEqual(observed_seed(0, 11), observed_seed(256, 11))
        self.assertEqual(len({observed_seed(t, 11) for t in range(512)}), 256)

    def test_known_implementation_flagged(self):
        source = ("fn init_stack() { let aux = [(25, random_at)]; "
                  "*b = (ticks as u8).wrapping_mul(31); }\n// ---- dispatch")
        self.assertTrue(any(x.startswith("FAIL: AT_RANDOM") for x in assess(source)))

    def test_unknown_provider_is_not_approved(self):
        source = "fn init_stack() { let aux = [(25, random_at)]; }\n// ---- dispatch"
        self.assertTrue(assess(source)[0].startswith("UNKNOWN:"))

    def test_unknown_layout_is_not_approved(self):
        self.assertTrue(assess("modified code")[0].startswith("UNKNOWN:"))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("source", nargs="?", type=Path)
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()
    if args.self_test:
        suite = unittest.defaultTestLoader.loadTestsFromTestCase(KnownImplementationTests)
        return 0 if unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful() else 1
    if args.source is None:
        ap.error("provide linux.rs or --self-test")
    findings = assess(args.source.read_text(encoding="utf-8"))
    for finding in findings:
        print(finding)
    print("Source audit only: passing this scan is never proof of secure randomness.")
    return 1 if any(x.startswith("FAIL:") for x in findings) else 2


if __name__ == "__main__":
    raise SystemExit(main())
