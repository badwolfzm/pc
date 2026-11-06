"""Fail-closed preflight for a single Warden swarm guest's QEMU argv.

This is a host-side command policy, not a guest memory or GPU measurement.
Only an operator-pinned, exact argv template is accepted; remote jobs cannot
supply additional flags, paths, or substitutions.
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path


class PolicyError(ValueError):
    pass


def validate(candidate: list[str], approved: list[str]) -> None:
    if not isinstance(approved, list) or not isinstance(candidate, list) or not approved or not all(isinstance(x, str) and x for x in approved):
        raise PolicyError("approved argv must contain nonempty strings")
    if not all(isinstance(x, str) and x for x in candidate) or candidate != approved:
        raise PolicyError("argv differs from operator-approved immutable template")
    if Path(approved[0]).name not in ("qemu-system-x86_64", "qemu-system-x86_64.exe"):
        raise PolicyError("approved executable must be qemu-system-x86_64")
    if approved.count("-smp") != 1 or approved.count("-m") != 1:
        raise PolicyError("require exactly one -smp and -m option")
    try:
        smp = approved[approved.index("-smp") + 1]
        memory = approved[approved.index("-m") + 1]
    except IndexError as exc:
        raise PolicyError("missing QEMU option value") from exc
    if smp != "1":
        raise PolicyError("exactly one vCPU required")
    if memory != "512M":
        raise PolicyError("require 512 MiB QEMU guest RAM")
    # Exact argv equality is the primary safeguard. These flags remain
    # disallowed even when mistakenly added to an operator-approved template.
    blocked = {"-monitor", "-qmp", "-qmp-pretty", "-gdb", "-S", "-daemonize",
               "-incoming", "-chardev", "-fsdev", "-virtfs", "-device-loader"}
    if any(flag in blocked for flag in approved):
        raise PolicyError("disallowed QEMU control or host-access flag")
    # The GPU's isolation and its host-side VRAM allocations cannot be proved
    # by parsing QEMU flags; these require a separate device/runtime audit.


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--policy", required=True, type=Path,
                        help="operator-owned JSON file containing approved_argv")
    parser.add_argument("--candidate", required=True, type=Path,
                        help="JSON file containing candidate_argv")
    args = parser.parse_args()
    try:
        policy = json.loads(args.policy.read_text())
        candidate = json.loads(args.candidate.read_text())
        validate(candidate["candidate_argv"], policy["approved_argv"])
    except (OSError, KeyError, TypeError, ValueError, json.JSONDecodeError) as exc:
        print(json.dumps({"accepted": False, "reason": str(exc)}))
        return 1
    print(json.dumps({"accepted": True, "scope": "argv preflight only"}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
