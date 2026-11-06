#!/usr/bin/env python3
"""Conservative source-level inventory of Warden Linux lifecycle syscall gaps.

Not a Rust interpreter, runtime test, or security proof. Only a literal
ret(Ok(0)) arm of pass() is called silent_success.
"""
import argparse
import json
import re
import sys
from pathlib import Path

FAMILIES = {
    "process": ("clone", "clone3", "fork", "vfork", "execve", "execveat", "wait4", "waitid"),
    "signal": ("rt_sigaction", "rt_sigprocmask", "rt_sigreturn", "rt_sigpending", "rt_sigtimedwait",
               "rt_sigqueueinfo", "rt_sigsuspend", "sigaltstack", "kill", "tkill", "tgkill",
               "rt_tgsigqueueinfo", "pidfd_send_signal", "signalfd", "signalfd4"),
    "futex": ("futex", "futex_waitv", "futex_wake", "futex_wait", "futex_requeue",
              "set_robust_list", "get_robust_list"),
}
TARGETS = {name: family for family, names in FAMILIES.items() for name in names}
ARM = re.compile(r'^[ \t]*(?P<names>"[a-zA-Z0-9_]+"(?:[ \t]*\|[ \t]*"[a-zA-Z0-9_]+")*)[ \t]*=>[ \t]*(?P<body>[^\n]+)', re.MULTILINE)
NAME = re.compile(r'"([a-zA-Z0-9_]+)"')


def extract_pass_arms(source):
    match = re.search(r'\bfn\s+pass\s*\(', source)
    if not match:
        raise ValueError("pass() dispatcher not found; inspection required")
    start = source.find("match name {", match.end())
    end = source.find("\n/// Mediated tier:", start)
    if start < 0 or end < 0:
        raise ValueError("pass() boundaries unrecognized; inspection required")
    # Ignore commented-out examples. This remains a conservative source scanner,
    # not a full Rust parser.
    region = source[start:end]
    region = re.sub(r"/\*.*?\*/", "", region, flags=re.DOTALL)
    region = re.sub(r"//[^\n]*", "", region)
    arms = {}
    for arm in ARM.finditer(region):
        body = arm.group("body").strip()
        for name in NAME.findall(arm.group("names")):
            if name in arms:
                raise ValueError(f"duplicate pass() arm for {name}")
            arms[name] = body
    return arms


def audit(abi, source):
    if not isinstance(abi, dict) or not isinstance(abi.get("syscalls"), list):
        raise ValueError("invalid ABI document")
    arms = extract_pass_arms(source)
    names = set()
    rows = []
    for entry in abi["syscalls"]:
        if not isinstance(entry, dict) or not isinstance(entry.get("name"), str) or not isinstance(entry.get("class"), str):
            raise ValueError("malformed syscall entry")
        name = entry["name"]
        if name in names:
            raise ValueError(f"duplicate ABI syscall name: {name}")
        names.add(name)
        if name not in TARGETS:
            continue
        tier = entry["class"]
        body = arms.get(name)
        if tier == "pass" and body is not None:
            status = "silent_success" if re.match(r'ret\(Ok\(0\)\)\s*,?(?:\s*//.*)?$', body) else "explicit_pass_arm"
        elif tier == "pass":
            status = "no_explicit_pass_arm"
        else:
            status = "outside_pass_dispatch"
        rows.append({"name": name, "number": entry.get("nr"), "family": TARGETS[name],
                     "abi_class": tier, "source_status": status})
    missing = sorted(set(TARGETS) - names)
    return {"summary": {"inspected": len(rows),
                        "silent_success": sum(r["source_status"] == "silent_success" for r in rows),
                        "missing_abi_entries": len(missing)},
            "missing_abi_entries": missing,
            "rows": sorted(rows, key=lambda r: (r["family"], r["number"] if isinstance(r["number"], int) else -1))}


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--abi", type=Path, default=Path("abi/warden-abi.json"))
    p.add_argument("--linux", type=Path, default=Path("crates/warden-kernel/src/linux.rs"))
    p.add_argument("--assert-no-silent-success", action="store_true")
    args = p.parse_args(argv)
    try:
        output = audit(json.loads(args.abi.read_text(encoding="utf-8")),
                       args.linux.read_text(encoding="utf-8"))
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"audit setup error: {exc}", file=sys.stderr)
        return 2
    print(json.dumps(output, indent=2, sort_keys=True))
    return int(args.assert_no_silent_success and (output["summary"]["silent_success"] > 0 or output["summary"]["missing_abi_entries"] > 0))


if __name__ == "__main__":
    sys.exit(main())
