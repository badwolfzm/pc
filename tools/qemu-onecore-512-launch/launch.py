#!/usr/bin/env python3
"""Run a constrained QEMU invocation, using the merged #221 argv preflight."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import stat
import subprocess
import sys

CHECKER = Path(__file__).resolve().parents[1] / "qemu-onecore-512-argv" / "check.py"
spec = importlib.util.spec_from_file_location("qemu_onecore_preflight", CHECKER)
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)

def run(argv, timeout_seconds=120):
    """Validate exactly the argv to be passed to exec, with no shell."""
    checked = checker.verify(argv)
    if type(timeout_seconds) is not int or not 1 <= timeout_seconds <= 3600:
        raise ValueError("timeout must be 1..3600 integer seconds")
    binary = Path(argv[0])
    if not binary.is_absolute():
        raise ValueError("QEMU executable path must be absolute")
    # Resolve paths and verify existence without silently changing the tested argv.
    target = binary.resolve(strict=True)
    mode = target.stat().st_mode
    if not stat.S_ISREG(mode) or not os.access(target, os.X_OK):
        raise ValueError("QEMU executable must be an executable regular file")
    # This is just evidence of a configured launch, not guest execution success.
    process = subprocess.run(argv, shell=False, timeout=timeout_seconds, check=False)
    return {"configured": checked, "process_exit_code": process.returncode,
            "actual_qemu_binary_verified": False,
            "native_warden_boot_verified": False,
            "doom_gameplay_verified": False}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("argv_json", type=Path)
    parser.add_argument("--timeout", type=int, default=120)
    args = parser.parse_args()
    try:
        if args.argv_json.stat().st_size > 65536:
            raise ValueError("argv JSON exceeds 65536 bytes")
        argv = json.loads(args.argv_json.read_text(encoding="utf-8"))
        result = run(argv, args.timeout)
    except (ValueError, OSError, json.JSONDecodeError) as error:
        parser.error(str(error))
    except subprocess.TimeoutExpired:
        print("QEMU process timed out", file=sys.stderr)
        return 124
    print(json.dumps(result, sort_keys=True))
    return result["process_exit_code"] if result["process_exit_code"] >= 0 else 128 + -result["process_exit_code"]

if __name__ == "__main__":
    sys.exit(main())
