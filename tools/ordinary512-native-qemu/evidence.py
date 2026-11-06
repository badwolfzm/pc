#!/usr/bin/env python3
"""Extract *limited* native Warden Pong frame allocator evidence from QEMU logs.

Does NOT claim allocator frames = total physical guest RAM usage.
"""
import argparse
import json
from pathlib import Path
import re
import sys

END = re.compile(r"(?m)^pong: screen hash (0x[0-9a-f]+), kernel heap carved ([0-9]+) more bytes, ([0-9]+) frames \(([0-9]+) KiB\) of physical memory in use, ([0-9]+) timer ticks$")
PHASE = re.compile(r"(?m)^pong: phase A, gameplay paced to 30 fps for 3 s: ([0-9]+) frames = ([0-9]+(?:\.[0-9]+)?) fps,")
SUCCESS = "ALL HELD: 15 checks, 0 failed"
FLAG = "-m 512M -smp 1"

class Invalid(ValueError):
    pass

def extract(log, manifest):
    if not isinstance(manifest, dict) or manifest.get("profile") != "native-pong-qemu-512m-smp1" or manifest.get("qemu_option") != FLAG or manifest.get("guest_ram_bytes") != 512 * 1024 * 1024 or manifest.get("guest_vcpus") != 1:
        raise Invalid("512 MiB / one-vCPU manifest missing or invalid")
    if "PONG RUN FAILED" in log or re.search(r"(?m)^FAIL ", log):
        raise Invalid("kernel reported a failure")
    if len(re.findall(r"(?m)^" + re.escape(SUCCESS) + r"$", log)) != 1:
        raise Invalid("missing exactly one 15-check kernel success summary")
    end = END.findall(log)
    phases = PHASE.findall(log)
    if len(end) != 1 or len(phases) != 1:
        raise Invalid("expected one kernel frame summary and one paced phase")
    screen_hash, carved, frames, kib, ticks = end[0]
    paced_frames, fps = phases[0]
    f, k, p, rate = int(frames), int(kib), int(paced_frames), float(fps)
    if not f or f * 4 != k or p < 1 or rate <= 0:
        raise Invalid("impossible reported Warden frame accounting or pace")
    return {
        "evidence_kind": "native-warden-pong-qemu-serial",
        "guest_ram_configured_bytes": manifest["guest_ram_bytes"],
        "guest_vcpus_configured": 1,
        "kernel_checks_passed": 15,
        "allocator_frames_in_use_at_end": f,
        "allocator_frame_bytes_at_end": f * 4096,
        "reported_additional_kernel_heap_carved_bytes": int(carved),
        "timer_ticks": int(ticks),
        "paced_phase_frames": p, "paced_phase_fps_reported": rate,
        "screen_hash": screen_hash,
        "scope": "Verified kernel-reported allocator frame count and Pong pace only. NOT full physical RAM accounting, peak RSS, host core use, or modern-game performance."
    }

def main(argv=None):
    p = argparse.ArgumentParser()
    p.add_argument("serial_log", type=Path)
    p.add_argument("manifest", type=Path)
    p.add_argument("output", type=Path)
    a = p.parse_args(argv)
    try:
        log = a.serial_log.read_text()
        m = json.loads(a.manifest.read_text())
        report = extract(log, m)
        a.output.parent.mkdir(parents=True, exist_ok=True)
        a.output.write_text(json.dumps(report, sort_keys=True, indent=2) + "\n")
        print(json.dumps(report, sort_keys=True))
        return 0
    except (OSError, UnicodeError, Invalid, ValueError) as exc:
        print("NATIVE512 EVIDENCE NOT ACCEPTED:", exc, file=sys.stderr)
        return 1

if __name__ == "__main__":
    sys.exit(main())
