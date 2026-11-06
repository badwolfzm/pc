#!/usr/bin/env python3
"""Deterministic, read-only Linux ELF compatibility inventory for Warden.

This tool never executes scanned programs. Candidate means ELF loader
preflight only, not ABI compatibility or actual Warden execution.
"""
import argparse
from collections import Counter
import json
import os
from pathlib import Path
import stat
import sys

from preflight import inspect_bytes

DEFAULT_MAX_BYTES = 16 * 1024 * 1024
DEFAULT_MAX_FILES = 1000


def discover(paths, max_files):
    """Yield a bounded sorted set of regular file paths, never follow symlinks."""
    seen = set()
    count = 0
    for base in sorted(set(map(str, paths))):
        root = Path(base)
        mode = root.lstat().st_mode
        if stat.S_ISLNK(mode):
            continue
        if stat.S_ISREG(mode):
            if root.as_posix() not in seen:
                seen.add(root.as_posix())
                yield root
                count += 1
                if count >= max_files:
                    return
        elif stat.S_ISDIR(mode):
            for d, folders, files in os.walk(root, followlinks=False):
                folders[:] = sorted(n for n in folders
                                    if not (Path(d) / n).is_symlink())
                for name in sorted(files):
                    p = Path(d) / name
                    if p.as_posix() in seen:
                        continue
                    if not stat.S_ISREG(p.lstat().st_mode):
                        continue
                    seen.add(p.as_posix())
                    yield p
                    count += 1
                    if count >= max_files:
                        return


def inventory(paths, target="linux", max_files=DEFAULT_MAX_FILES,
              max_bytes=DEFAULT_MAX_BYTES):
    if not paths or max_files < 1 or max_bytes < 64:
        raise ValueError("paths required, max_files >= 1 and max_bytes >= 64")
    results = []
    reasons = Counter()
    scanned = 0
    non_elf = 0
    errors = 0
    candidates = 0
    for path in discover(paths, max_files):
        scanned += 1
        name = path.as_posix()
        try:
            size = path.stat().st_size
            if size > max_bytes:
                results.append({"path": name, "classification": "skipped_oversize",
                                "bytes": size})
                continue
            with path.open("rb") as file:
                magic = file.read(4)
                if magic != b"\x7fELF":
                    non_elf += 1
                    continue
                content = magic + file.read()
            result = inspect_bytes(content, target)
            row = {"path": name, "classification": result["classification"],
                   "reasons": result["reasons"]}
            results.append(row)
            reasons.update(result["reasons"])
            candidates += int(result["candidate_for_current_loader"])
        except OSError as ex:
            errors += 1
            results.append({"path": name, "classification": "read_error",
                            "error": type(ex).__name__})
    return {
        "schema": 1, "target": target,
        "disclaimer": "Static ELF preflight only. No Linux software was executed.",
        "limits": {"max_files": max_files, "max_bytes": max_bytes},
        "summary": {"files_checked": scanned, "non_elf": non_elf,
                    "elf_candidates": candidates,
                    "elf_blocked": sum(x["classification"] == "blocked" for x in results),
                    "oversize": sum(x["classification"] == "skipped_oversize" for x in results),
                    "read_errors": errors},
        "blockers": dict(sorted(reasons.items())),
        "results": results,
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", nargs="+", type=Path,
                        help="files or directories to inventory; symbolic links skipped")
    parser.add_argument("--target", choices=("linux", "native"), default="linux")
    parser.add_argument("--max-files", type=int, default=DEFAULT_MAX_FILES)
    parser.add_argument("--max-bytes", type=int, default=DEFAULT_MAX_BYTES)
    args = parser.parse_args(argv)
    try:
        report = inventory(args.paths, args.target, args.max_files, args.max_bytes)
    except (ValueError, OSError) as ex:
        print(json.dumps({"error": str(ex), "classification": "error"}))
        return 2
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0 if not report["summary"]["read_errors"] else 2


if __name__ == "__main__":
    sys.exit(main())
