#!/usr/bin/env python3
"""Build pinned genuine Doom and measure an ACTUAL host game run on one CPU.

The host simulator/sampler never represents total Warden guest RAM.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
PIN = "e257a09527062bfb986b6e5e0780839b6cbd5b71"
FREEDOOM1_SHA256 = "7323bcc168c5a45ff10749b339960e98314740a734c30d4b9f3337001f9e703d"


def verify_source_and_asset(source, wad):
    if not source.is_dir() or not wad.is_file() or wad.is_symlink():
        raise ValueError("pinned source directory and regular game asset required")
    sha = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
    if sha != PIN:
        raise ValueError(f"unapproved upstream source SHA {sha}")
    if not (12 <= wad.stat().st_size <= 64 * 1024 * 1024):
        raise ValueError("WAD size outside supported test range")
    with wad.open("rb") as asset:
        digest = hashlib.file_digest(asset, "sha256").hexdigest()
    if digest != FREEDOOM1_SHA256:
        raise ValueError(f"Freedoom asset SHA mismatch: {digest}")
    return digest


def load_sampler(path):
    spec = importlib.util.spec_from_file_location("warden_host_sampler", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.sample


def run(source, wad, cpu, timeout):
    # Validate raw path before resolving to preserve symlink rejection.
    digest = verify_source_and_asset(source, wad)
    source, wad = source.resolve(), wad.resolve()
    with tempfile.TemporaryDirectory(prefix="warden-freedoom-measure-") as directory:
        work = Path(directory)
        subprocess.run([
            sys.executable, str(ROOT / "tools/doom-upstream-compile/inventory.py"),
            "--source", str(source), "--output", str(work / "inventory")
        ], check=True)
        inventory = json.loads((work / "inventory/inventory.json").read_text())
        objects = [str(Path(f["object"])) for f in inventory["files"]]
        if len(objects) != 80:
            raise ValueError(f"unexpected genuine engine translation-unit count: {len(objects)}")
        exe = work / "doom-freedoom-host"
        subprocess.run([
            "cc", "-std=gnu11", "-O2", "-Wall", "-Wextra",
            "-DDOOMGENERIC_RESX=320", "-DDOOMGENERIC_RESY=200",
            "-I" + str(source), str(ROOT / "tools/doom-real-freedoom-host/main.c"),
            *objects, "-lm", "-o", str(exe)
        ], check=True)
        sampler = load_sampler(ROOT / "tools/ordinary-pc-host-sampler/sample.py")
        observation = sampler([str(exe), str(wad)], cpu=cpu, budget_mib=512,
                              period_ms=10, timeout_sec=timeout)
        observation.update({
            "test": "genuine Freedoom Phase 1 on Linux host, 320x200, headless",
            "upstream_commit": PIN,
            "freedoom1_sha256": digest,
            "freedoom1_bytes": wad.stat().st_size,
            "engine_objects": len(objects),
            "display": "headless software CPU pixels; not dedicated GPU",
            "guest_ram_cap_enforced": False,
            "native_warden_guest": False,
            "host_memory_is_not_guest_memory": True,
        })
        # Avoid non-portable temp executable command path in archived evidence.
        observation["command"] = ["pinned-genuine-doom-host", "freedoom1.wad"]
        print("HOST_GAME_MEASUREMENT=" + json.dumps(observation, sort_keys=True), flush=True)
        if observation["exit_code"] != 0 or observation["timeout"] or observation["sample_count"] < 1:
            raise RuntimeError("real Doom host workload failed or lacked measured samples")
        return observation


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--source", required=True, type=Path)
    p.add_argument("--wad", required=True, type=Path)
    p.add_argument("--cpu", type=int, help="allowed logical host CPU; default is first allowed")
    p.add_argument("--timeout", type=float, default=20)
    a = p.parse_args()
    if not sys.platform.startswith("linux") or not hasattr(os, "sched_getaffinity"):
        p.error("Linux host CPU affinity and /proc required")
    cpu = a.cpu if a.cpu is not None else min(os.sched_getaffinity(0))
    try:
        run(a.source, a.wad, cpu, a.timeout)
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as exc:
        p.error(str(exc))


if __name__ == "__main__":
    main()
