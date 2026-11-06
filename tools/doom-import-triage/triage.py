#!/usr/bin/env python3
"""Triage actual unresolved Warden/Doom engine imports, without claiming runtime closure."""
import argparse
import json
from pathlib import Path

# Classification is a review aid, NOT a grant of capabilities.
# Host I/O requires explicit Warden mediation; memory-only formatting does not.
HOST = {"fopen", "fopen64", "freopen", "fdopen", "fclose", "fread", "fwrite",
        "fseek", "fseeko", "ftell", "ftello", "fflush", "fprintf", "vfprintf",
        "printf", "vprintf", "perror", "puts", "fgets", "fputs", "fgetc",
        "fputc", "getc", "putc", "getchar", "putchar", "remove", "rename",
        "stat", "lstat", "fstat", "opendir", "readdir", "closedir",
        "system", "popen", "pclose", "execve", "execl", "execvp", "fork",
        "vfork", "posix_spawn", "socket", "connect", "accept", "bind",
        "listen", "send", "recv", "sendto", "recvfrom", "open", "open64",
        "openat", "creat", "close", "read", "write", "pread", "pwrite",
        "lseek", "unlink", "access", "mmap", "mprotect", "munmap",
        "brk", "sbrk", "ioctl", "dlopen", "dlsym", "dlclose"}
BOUNDED_FORMAT = {"snprintf", "vsnprintf"}
UNBOUNDED_FORMAT = {"sprintf", "vsprintf"}
ALLOC = {"malloc", "calloc", "realloc", "free", "aligned_alloc"}
MATH = {"sin", "cos", "tan", "atan2", "sqrt", "pow", "floor", "ceil", "fabs"}
LIBC_STATE = {"__errno_location", "__ctype_b_loc", "__ctype_tolower_loc",
              "__ctype_toupper_loc", "setlocale", "getenv"}
MAX_SYMBOLS = 10000
MAX_USERS = 1000


def validate(report):
    if not isinstance(report, dict):
        raise ValueError("report must be an object")
    if report.get("native_executable_built") is not False or report.get("native_warden_boot_verified") is not False:
        raise ValueError("only unresolved pre-boot linker reports accepted")
    symbols = report.get("unresolved_symbols")
    prov = report.get("unresolved_symbol_provenance")
    if not isinstance(symbols, list) or not isinstance(prov, dict) or len(symbols) > MAX_SYMBOLS:
        raise ValueError("invalid unresolved symbol inventory")
    if len(symbols) != len(set(map(str, symbols))):
        raise ValueError("duplicate symbols")
    if report.get("unresolved_symbol_count") != len(symbols):
        raise ValueError("unresolved count mismatch")
    if set(prov) != set(symbols):
        raise ValueError("provenance must cover precisely all missing symbols")
    for sym in symbols:
        if not isinstance(sym, str) or not sym or len(sym) > 256 or any(ord(c) < 33 or ord(c) > 126 for c in sym):
            raise ValueError("invalid symbol name")
        owners = prov[sym]
        if (not isinstance(owners, list) or not owners or len(owners) > MAX_USERS
                or any(not isinstance(o, str) or not o or len(o) > 256 or
                       "/" in o or "\\" in o or any(ord(c) < 33 or ord(c) > 126 for c in o)
                       for o in owners) or len(set(owners)) != len(owners)):
            raise ValueError("invalid object-file provenance: " + sym)
    if not isinstance(report.get("upstream_commit"), str) or len(report["upstream_commit"]) != 40 or any(c not in "0123456789abcdef" for c in report["upstream_commit"]):
        raise ValueError("missing immutable upstream commit")
    return symbols, prov


def classify(sym):
    if sym in HOST or sym.startswith("__libc_"):
        return "privileged_host_boundary", "Require reviewed Warden capability mediation or safe removal; no host passthrough"
    if sym in BOUNDED_FORMAT:
        return "bounded_formatting", "Use a tested bounded freestanding formatter; no host I/O or implicit grants"
    if sym in UNBOUNDED_FORMAT:
        return "unsafe_formatting", "Audit format and destination bounds; prefer reviewed bounded replacement"
    if sym.startswith("__") and sym.endswith("_chk"):
        return "checked_runtime", "Implement verified bounds checks in guest; do not silently discard fortify semantics"
    if sym in ALLOC:
        return "bounded_memory", "Use independently reviewed bounded guest heap with real memory accounting"
    if sym in LIBC_STATE:
        return "guest_runtime_state", "Provide correct guest-local or per-thread semantics, not dummy stubs"
    if sym in MATH:
        return "numerical_runtime", "Provide tested freestanding semantics without host dependencies"
    return "unclassified", "Inspect the authentic call site before deciding on a safe replacement"


def triage(report):
    symbols, prov = validate(report)
    weights = {"privileged_host_boundary": 0, "unsafe_formatting": 1,
               "checked_runtime": 2, "guest_runtime_state": 3,
               "bounded_memory": 4, "unclassified": 5,
               "bounded_formatting": 6, "numerical_runtime": 7}
    rows = []
    for symbol in symbols:
        kind, action = classify(symbol)
        rows.append({"symbol": symbol, "category": kind, "importing_object_count": len(prov[symbol]),
                     "importing_objects": sorted(prov[symbol]), "next_action": action})
    rows.sort(key=lambda row: (weights[row["category"]], -row["importing_object_count"], row["symbol"]))
    return {
        "source_upstream_commit": report["upstream_commit"],
        "source_unresolved_count": len(symbols),
        "analysis_only": True,
        "native_executable_built": False,
        "native_warden_boot_verified": False,
        "guest_ram_measured": False,
        "game_fps_measured": False,
        "ranked_imports": rows,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--gate", type=Path, required=True, help="JSON emitted by genuine doom-native-link-gate")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.gate.stat().st_size > 8 * 1024 * 1024:
        parser.error("report too large")
    try:
        result = triage(json.loads(args.gate.read_text(encoding="utf-8")))
    except (ValueError, KeyError, TypeError) as exc:
        parser.error(str(exc))
    args.output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"Triage: {result['source_unresolved_count']} genuine unresolved symbols; analysis only")


if __name__ == "__main__":
    main()
