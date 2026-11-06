#!/usr/bin/env python3
"""A real-QEMU smoke of the host launcher, NOT a Warden/Doom test.

Build a standalone Multiboot fixture and run it twice through run.py. Verify
serial content, QEMU status and pinned binary provenance on both boots.
"""
import hashlib
import json
import pathlib
import shutil
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
RUNNER = HERE / "run.py"
EXPECTED = b"WARDEN_SMOKE\n"


def execute(argv):
    proc = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          text=True, timeout=45)
    if proc.returncode:
        raise RuntimeError(f"command failed ({proc.returncode}): {argv[0]}\n{proc.stderr[-2000:]}")
    return proc


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    for tool in ("qemu-system-x86_64", "as", "ld"):
        if shutil.which(tool) is None:
            raise RuntimeError(f"missing required real smoke-test tool: {tool}")
    qemu = pathlib.Path(shutil.which("qemu-system-x86_64")).resolve()
    with tempfile.TemporaryDirectory(prefix="warden-qemu-smoke-") as tmp:
        root = pathlib.Path(tmp)
        obj, kernel = root / "fixture.o", root / "fixture.elf"
        execute(["as", "--32", str(HERE / "smoke-fixture.S"), "-o", str(obj)])
        execute(["ld", "-m", "elf_i386", "-T", str(HERE / "smoke-fixture.ld"),
                 "-o", str(kernel), str(obj)])
        summaries = []
        for index in (1, 2):
            log, report = root / f"boot{index}.log", root / f"boot{index}.json"
            execute([sys.executable, str(RUNNER), "--qemu", str(qemu),
                     "--qemu-sha256", digest(qemu), "--kernel", str(kernel),
                     "--kernel-sha256", digest(kernel), "--accel", "tcg",
                     "--timeout", "30", "--log", str(log),
                     "--report", str(report)])
            data = json.loads(report.read_text(encoding="utf-8"))
            if (EXPECTED not in log.read_bytes() or data["returncode"] != 1
                    or data["timed_out"] or data["log_overflow"]
                    or data["debug_exit_status"] != 0
                    or data["serial_log_sha256"] != digest(log)
                    or data["guest_boot_verified"] or data["doom_executed_verified"]):
                raise RuntimeError(f"real QEMU fixture boot {index} failed validation")
            summaries.append({"run": index, "qemu_returncode": data["returncode"],
                              "serial_marker_verified": True, "fixture_only": True})
        print(json.dumps({"real_qemu_launch_fixture_passed": True,
                          "warden_boot_verified": False, "doom_boot_verified": False,
                          "runs": summaries}, sort_keys=True))


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, OSError, subprocess.TimeoutExpired) as exc:
        print("real QEMU fixture failed: " + str(exc), file=sys.stderr)
        sys.exit(1)
