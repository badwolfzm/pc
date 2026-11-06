#!/usr/bin/env python3
"""Audit literal sscanf formats in pinned genuine Doom source (not full C parser)."""
import argparse
import json
import pathlib
import re

EXPECTED = {
    "m_misc.c": [" 0x%x", " 0X%x", " 0%o", " %d"],
    "m_config.c": ["%x", "%i"],
}
EXPECTED_OBJECTS = {"upstream_m_misc.o", "upstream_m_config.o"}
CALL = re.compile(r'\bsscanf\s*\(\s*[^,();]*,\s*"([^"\\]*)"\s*,', re.S)
PIN = "e257a09527062bfb986b6e5e0780839b6cbd5b71"


def remove_comments(text):
    """Replace C comments outside quoted literals, preserving line breaks."""
    pat = re.compile(r'("(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|/\*[\s\S]*?\*/|//[^\n]*)')
    def replace(match):
        part = match.group()
        if part.startswith(("/*", "//")):
            return "".join("\n" if char == "\n" else " " for char in part)
        return part
    return pat.sub(replace, text)


def audit(source, gate):
    if gate.get("upstream_commit") != PIN:
        raise ValueError("unexpected pinned upstream revision")
    prov = gate.get("unresolved_symbol_provenance")
    if not isinstance(prov, dict) or set(prov.get("__isoc99_sscanf", [])) != EXPECTED_OBJECTS:
        raise ValueError("real-link import object provenance changed")
    # Restrict audit to the real engine manifest, excluding intentionally
    # substituted platform backends. The compiler/linker gate pins this manifest.
    report = {}
    for filename, formats in EXPECTED.items():
        data = (source / filename).read_text(encoding="utf-8")
        found = CALL.findall(remove_comments(data))
        if sorted(found) != sorted(formats):
            raise ValueError(f"{filename}: unsupported or missing sscanf formats: {found!r}")
        report[filename] = found
    return report


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--source", required=True, type=pathlib.Path)
    p.add_argument("--gate", required=True, type=pathlib.Path)
    args = p.parse_args()
    try:
        result = audit(args.source, json.loads(args.gate.read_text()))
    except (OSError, ValueError, TypeError) as exc:
        p.error(str(exc))
    print(json.dumps(result, indent=2, sort_keys=True))
    print("PASS: all six pinned Doom sscanf call formats accounted for; no expanded ABI claim")


if __name__ == "__main__":
    main()
