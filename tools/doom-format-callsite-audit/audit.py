#!/usr/bin/env python3
"""Conservative source-level printf-family format inventory; NOT a C parser."""
import argparse
import importlib.util
import json
import pathlib
import re
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[2]
PIN = "e257a09527062bfb986b6e5e0780839b6cbd5b71"
FORMAT_ARG = {"printf": 0, "fprintf": 1, "sprintf": 1, "snprintf": 2,
              "vprintf": 0, "vfprintf": 1, "vsprintf": 1, "vsnprintf": 2}
TOKENS = re.compile(r'(?P<skip>\s+|//[^\n]*|/\*[\s\S]*?\*/)|'
                    r'(?P<string>"(?:\\.|[^"\\])*")|'
                    r"(?P<char>'(?:\\.|[^'\\])*')|"
                    r'(?P<ident>[A-Za-z_]\w*)|(?P<punct>[^\s])')
SPEC = re.compile(r'%(%|(?:[-+ #0]*)(?:\*|\d*)(?:\.(?:\*|\d*))?(?:hh|ll|[hljztL])?[A-Za-z%])')
SUPPORTED = set("diuoxXsc%")
# Current merged freestanding runtime + console covers printf, snprintf,
# vsnprintf; the remaining stdio-family calls are NOT implemented ABI.
MISSING_RUNTIME_FUNCTIONS = {"fprintf", "sprintf", "vprintf", "vfprintf", "vsprintf"}

def tokenize(source):
    tokens = []
    pos = 0
    while pos < len(source):
        m = TOKENS.match(source, pos)
        if m is None:
            raise ValueError("unrecognized token")
        kind = m.lastgroup
        if kind != "skip":
            tokens.append((kind, m.group(), source.count("\n", 0, pos) + 1))
        pos = m.end()
    return tokens

def args_at(tokens, index):
    if index >= len(tokens) or tokens[index][1] != "(":
        return None
    depth = 0
    args = [[]]
    for k in range(index, len(tokens)):
        typ, token, _ = tokens[k]
        if token == "(":
            depth += 1
            if depth > 1:
                args[-1].append(tokens[k])
        elif token == ")":
            depth -= 1
            if depth == 0:
                return args
            args[-1].append(tokens[k])
        elif token == "," and depth == 1:
            args.append([])
        else:
            args[-1].append(tokens[k])
    return None

def fmt_problems(raw):
    issues = []
    i = 0
    while i < len(raw):
        if raw[i] != "%":
            i += 1
            continue
        match = SPEC.match(raw, i)
        if match is None:
            issues.append("malformed-percent")
            i += 1
            continue
        directive = match.group()
        conversion = directive[-1]
        if conversion not in SUPPORTED:
            issues.append("unsupported-" + conversion)
        elif conversion == "%":
            if directive != "%%":
                issues.append("complex-percent")
        elif "*" in directive:
            issues.append("dynamic-width-or-precision")
        if "$" in directive:
            issues.append("positional-format")
        i = match.end()
    return sorted(set(issues))

def scan_source(source, file):
    tokens = tokenize(source)
    results = []
    for i, (kind, name, line) in enumerate(tokens):
        if kind != "ident" or name not in FORMAT_ARG or i + 1 >= len(tokens) or tokens[i + 1][1] != "(":
            continue
        args = args_at(tokens, i + 1)
        if args is None or len(args) <= FORMAT_ARG[name]:
            continue
        expr = args[FORMAT_ARG[name]]
        # Only raw adjacent string literals are treated as known format.
        if expr and all(t[0] == "string" for t in expr):
            raw = "".join(t[1][1:-1] for t in expr)
            issues = fmt_problems(raw)
            evidence = {"format_literal": raw[:240], "classification": "literal",
                        "problems": issues}
        else:
            evidence = {"format_literal": None, "classification": "dynamic-or-unparsed",
                        "problems": ["manual-review-required"]}
        if name in MISSING_RUNTIME_FUNCTIONS:
            evidence["problems"] = sorted(set(evidence["problems"] + ["unimplemented-stdio-function"]))
        results.append({"file": file, "line": line, "function": name, **evidence})
    return results

def run(source):
    source = source.resolve()
    revision = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"],
                                       text=True).strip()
    if revision != PIN:
        raise ValueError("wrong genuine upstream commit " + revision)
    inv_path = ROOT / "tools/doom-upstream-compile/inventory.py"
    spec = importlib.util.spec_from_file_location("doom_inventory_callsite", inv_path)
    inventory = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(inventory)
    selected = [p for p in inventory.object_list((source / "Makefile.soso").read_text())
                if p[:-2] + ".o" not in {"doomgeneric_soso.o", "w_file_stdc.o"}]
    if len(selected) != 79:
        raise ValueError("unexpected real Doom translation unit count")
    calls = []
    for relative in selected:
        path = source / relative
        calls.extend(scan_source(path.read_text(errors="replace"), relative))
    classifications = {"literal": 0, "dynamic-or-unparsed": 0}
    for row in calls:
        classifications[row["classification"]] += 1
    return {"upstream_commit": PIN, "real_engine_translation_units": 79,
            "callsites": calls, "counts": classifications,
            "manual_review_required": sum(bool(x["problems"]) for x in calls),
            "unimplemented_abi_callsites": sum("unimplemented-stdio-function" in x["problems"] for x in calls),
            "source_scanner_not_full_c_parser": True,
            "native_guest_boot_verified": False, "printf_runtime_compatibility_verified": False}

if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("--source", required=True, type=pathlib.Path)
    p.add_argument("--output", required=True, type=pathlib.Path)
    a = p.parse_args()
    report = run(a.source)
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(json.dumps({"count": len(report["callsites"]), "counts": report["counts"],
                      "manual_review_required": report["manual_review_required"]}, sort_keys=True))
