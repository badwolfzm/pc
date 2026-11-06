#!/usr/bin/env python3
"""Read-only current-kernel audit; reports missing console object identity."""
from pathlib import Path
import json
import re
ROOT=Path(__file__).resolve().parents[2]
src=(ROOT/"crates/warden-kernel/src/thread.rs").read_text(encoding="utf-8")
start=src.find("        SYS_CONSOLE_WRITE => {")
end=src.find("        SYS_INVOKE =>", start)
if start < 0 or end <= start:
    raise SystemExit("FAIL: console branch not identifiable; manual review required")
block=src[start:end]
checks={
    "requires_write_capability":'Rights::WRITE' in block,
    "requires_endpoint_kind":'ObjectKind::Endpoint' in block,
    "checks_console_object_identity":bool(re.search(r'\.object\s*==|\.object\s*!=|console_object|authorized_console_id',block)),
    "raw_serial_output_present":'serial::write_bytes' in block,
}
print(json.dumps({"source":"crates/warden-kernel/src/thread.rs","checks":checks,
                  "native_doom_console_security_verified":False,
                  "note":"source-only audit; no kernel tests executed; identity check must be reviewed"},sort_keys=True))
if not checks["requires_write_capability"] or not checks["requires_endpoint_kind"]:
    raise SystemExit("FAIL: kernel console permissions regressed")
# Fail only if policy *looks* fixed yet cannot be reviewed; never claim it is.
if not checks["checks_console_object_identity"]:
    print("KNOWN SECURITY BLOCKER: unrelated writable endpoints accepted by current source")
