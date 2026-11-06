#!/usr/bin/env python3
"""Compose existing Warden-native Doom CRT providers into one real-engine ET_REL.

The result is NOT a bootable Warden guest or a RAM/FPS benchmark. An external
snprintf/vsnprintf candidate must be staged from an exact reviewed Git SHA.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
GATE_PATH = ROOT / "tools/doom-native-link-gate/gate.py"
CANDIDATE_COMMIT = "a3529e43aff95423d0f7c12d78e33ab9e55e3fda"
EXPECTED = {
    "abs", "fabs",
    "strcasecmp", "strncasecmp", "strncpy", "strstr",
    "malloc", "calloc", "realloc", "free",
    "strdup", "__errno_location", "strtol", "__ctype_toupper_loc",
    "snprintf", "vsnprintf",
}
PROTECTED = {
    "system", "fopen", "fclose", "fread", "fwrite",
    "fseek", "ftell", "remove", "rename",
}
# All sources except the exact-pinned candidate are already merged in main.
PROVIDERS = (
    ("pure", "tools/doom-pure-crt-symbols/pure.c"),
    ("strings", "tools/doom-mini-crt/mini_extra.c"),
    ("arena", "tools/doom-freestanding-arena/arena.c"),
    ("heap_alias", "tools/doom-native-heap-closure/heap_alias.c"),
    ("strdup", "tools/doom-native-strdup-closure/strdup.c"),
    ("errno", "tools/doom-native-errno/errno.c"),
    ("strtol", "tools/doom-native-strtol/strtol.c"),
    ("ctype", "tools/doom-native-ctype/doom_ctype.c"),
    ("format", None),
)


def exec_checked(args):
    output = subprocess.run(args, text=True, capture_output=True)
    if output.returncode:
        raise RuntimeError("command failed: " + " ".join(map(str, args[:6])) +
                           "\n" + output.stderr[-6000:])
    return output.stdout


def load_gate():
    spec = importlib.util.spec_from_file_location("warden_native_gate_bundle", GATE_PATH)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def exports(path):
    """Read global definitions, including weak (which fail the ownership gate)."""
    lines = exec_checked(["nm", "-P", "-g", str(path)]).splitlines()
    definitions = set()
    undefined = set()
    for line in lines:
        bits = line.split()
        if len(bits) < 2:
            raise ValueError("unparseable nm global symbol: " + line)
        name, kind = bits[:2]
        if kind == "U":
            undefined.add(name)
        elif kind.upper() in {"T", "D", "B", "R", "W", "V", "C", "G", "S"}:
            definitions.add(name)
        else:
            raise ValueError("unexpected symbol type in object: " + line)
    return definitions, undefined


def ensure_unique(initial, symbol_sets):
    """Reject provider overlap with baseline and across providers."""
    owners = {symbol: "base" for symbol in initial}
    for filename, provided in symbol_sets:
        for symbol in provided:
            if symbol in owners:
                raise ValueError(
                    "duplicate global provider for " + symbol + ": " +
                    owners[symbol] + ", " + filename)
            owners[symbol] = filename
    return owners


def closure(before, after):
    before, after = set(before), set(after)
    if not EXPECTED.issubset(before):
        raise ValueError("missing expected real upstream import(s): " +
                         repr(sorted(EXPECTED - before)))
    if not PROTECTED.issubset(before):
        raise ValueError("protected host call missing from original")
    if not PROTECTED.issubset(after):
        raise ValueError("protected host API unexpectedly resolved")
    if after != before - EXPECTED:
        raise ValueError("unresolved imports not exactly the expected 16-symbol closure; added=" +
                         repr(sorted(after - before)) + " still-unresolved=" +
                         repr(sorted(after & EXPECTED)))
    return sorted(before - after)


def run(source, output, staged_format):
    source = source.resolve()
    staged_format = staged_format.resolve()
    if not staged_format.is_file():
        raise ValueError("missing external candidate format provider")
    output.mkdir(parents=True, exist_ok=True)
    gate = load_gate()
    base_report = gate.run(source, output / "base")
    base_obj = output / "base/all-objects.o"
    before = set(base_report["unresolved_symbols"])
    base_exports, _ = exports(base_obj)
    objs = []
    symbols = []
    manifests = []
    for name, relpath in PROVIDERS:
        code = staged_format if relpath is None else ROOT / relpath
        if not code.is_file():
            raise ValueError("missing merged source provider: " + str(code))
        obj = output / (name + ".o")
        command = [
            "cc", *gate.CFLAGS,
            "-DDOOM_ARENA_BYTES=1048576",
            "-I" + str(ROOT / "tools/doom-freestanding-arena"),
            "-c", str(code), "-o", str(obj),
        ]
        exec_checked(command)
        defined, missing = exports(obj)
        if not defined:
            raise ValueError("provider has no global definitions: " + name)
        if name == "format" and (defined != {"snprintf", "vsnprintf"} or missing):
            raise ValueError("format provider exports/imports wrong ABI symbols")
        symbols.append((name, defined))
        objs.append(obj)
        manifests.append({
            "name": name,
            "source": relpath or "pinned external PR #225",
            "sha256": hashlib.sha256(code.read_bytes()).hexdigest(),
            "symbols_exported": sorted(defined),
            "external_symbol_imports": sorted(missing),
        })
    ownership = ensure_unique(base_exports, symbols)
    if not EXPECTED.issubset(ownership):
        raise ValueError("expected new providers not defined")
    combined = output / "genuine-doom-with-native-runtime.o"
    exec_checked(["ld", "-r", "-o", str(combined), str(base_obj),
                  *(str(p) for p in objs)])
    remaining = set(gate.missing_symbols(exec_checked(["nm", "-g", "-u", str(combined)])))
    removed = closure(before, remaining)
    sections = exec_checked(["size", "-A", str(combined)])
    (output / "et-rel-sections.txt").write_text(sections)
    result = {
        "upstream_commit": gate.PIN,
        "original_engine_translation_units": base_report["real_upstream_source_count"],
        "warden_components": base_report["warden_component_count"],
        "format_candidate_pr": 225,
        "format_candidate_exact_commit": CANDIDATE_COMMIT,
        "format_candidate_not_yet_merged": True,
        "arena_bytes_reserved_in_BSS": 1048576,
        "crt_providers": manifests,
        "candidate_object_sha256": hashlib.sha256(combined.read_bytes()).hexdigest(),
        "original_undefined_count": len(before),
        "remaining_undefined_count": len(remaining),
        "exact_resolved_imports": removed,
        "remaining_imports": sorted(remaining),
        "protected_host_calls_unresolved": sorted(PROTECTED),
        "static_section_report": "et-rel-sections.txt",
        "output_format": "ET_REL relocatable partial link; NOT final ET_EXEC",
        "native_warden_boot_verified": False,
        "whole_guest_512m_measured": False,
        "fps_measured": None,
        "albion_compatibility_verified": False,
        "note": "All providers combined; no unreviewed system/file operation substitutions. No runtime/callsite test.",
    }
    (output / "report.json").write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    print(json.dumps({
        "original_missing": len(before),
        "remaining_missing": len(remaining),
        "resolved": removed,
        "runtime_status": "ET_REL only, no Warden boot",
    }, sort_keys=True))
    return result


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--source", type=Path, required=True,
                   help="Pinned upstream Doomgeneric source directory")
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--format-source", type=Path, required=True)
    x = p.parse_args()
    try:
        run(x.source, x.output, x.format_source)
    except (OSError, RuntimeError, ValueError, subprocess.CalledProcessError) as exc:
        p.error(str(exc))
