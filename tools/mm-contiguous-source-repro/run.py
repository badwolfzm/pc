#!/usr/bin/env python3
"""Compile and exercise the real Warden FrameAlloc implementation on Linux host.

No kernel tree edits. This is NOT a QEMU or guest test.
"""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
MM = ROOT / "crates/warden-kernel/src/mm.rs"
START = "pub const PAGE: u64 = 4096;"
END = "pub struct AddressSpace {"

def extract_mm(source):
    if source.count(START) != 1 or source.count(END) != 1:
        raise ValueError("unexpected mm.rs structure; review source before extracting")
    chunk = source.split(START, 1)[1].split(END, 1)[0]
    if chunk.count("pub fn alloc_contiguous(") != 1 or chunk.count("pub fn alloc(") != 1:
        raise ValueError("real FrameAlloc implementation not found")
    return "use std::vec::Vec;\nuse std::ptr;\n" + START + chunk

HARNESS = r"""
use std::alloc::{alloc_zeroed, dealloc, Layout};
struct Backing { ptr: *mut u8, layout: Layout }
impl Backing {
    fn new() -> Self {
        let layout = Layout::from_size_align(4096 * 8, 4096).unwrap();
        let ptr = unsafe { alloc_zeroed(layout) };
        assert!(!ptr.is_null(), "could not allocate aligned backing");
        Self { ptr, layout }
    }
    fn fa(&self) -> FrameAlloc {
        let start = self.ptr as u64;
        FrameAlloc::new(start, start + 4096 * 8)
    }
}
impl Drop for Backing {
    fn drop(&mut self) { unsafe { dealloc(self.ptr, self.layout) }; }
}
fn main() {
    let backing = Backing::new();
    let mut success = backing.fa();
    let first = success.alloc_contiguous(3).expect("ordinary contiguous allocation failed");
    assert_eq!(success.used, 3);
    assert_eq!(success.high_water(), 3);
    assert!(first % 4096 == 0);

    let mut zero = backing.fa();
    let before_zero = zero.used;
    let zero_result = zero.alloc_contiguous(0);
    let zero_side_effect = zero_result.is_some() || zero.used != before_zero;

    let mut fragmented = backing.fa();
    let a = fragmented.alloc().unwrap();
    let _b = fragmented.alloc().unwrap();
    let c = fragmented.alloc().unwrap();
    fragmented.free(a);
    fragmented.free(c);
    assert_eq!(fragmented.used, 1);
    let before = fragmented.used;
    let failed = fragmented.alloc_contiguous(2);
    let leak = failed.is_none() && fragmented.used != before;
    // A repaired allocator may correctly choose a new contiguous bump range.
    // Its successful request must account for exactly two newly owned frames.
    if let Some(start) = failed {
        assert_eq!(fragmented.used, before + 2);
        assert_eq!(start % 4096, 0);
    }
    // The reserved boot-module region may bisect the would-be run.
    let mut reserved = backing.fa();
    let base = backing.ptr as u64;
    reserved.reserve(base + 2 * PAGE, base + 4 * PAGE);
    let reserve_before = reserved.used;
    let reserve_result = reserved.alloc_contiguous(3);
    let reserve_leak = reserve_result.is_none() && reserved.used != reserve_before;
    if let Some(start) = reserve_result {
        assert!(start >= base + 4 * PAGE);
        assert_eq!(reserved.used, reserve_before + 3);
    }

    let mut exhausted = backing.fa();
    let exhaustion_before = exhausted.used;
    let exhaustion_result = exhausted.alloc_contiguous(9);
    let exhaustion_leak = exhaustion_result.is_none() && exhausted.used != exhaustion_before;
    assert!(exhaustion_result.is_none());

    println!("{{\"source\":\"actual FrameAlloc from mm.rs\",\"normal_contiguous_ok\":true,\"zero_count_has_side_effect\":{},\"noncontiguous_failure_leaks_used\":{},\"reserved_overlap_failure_leaks_used\":{},\"exhaustion_failure_leaks_used\":{},\"before_used\":{},\"after_used\":{},\"zero_after_used\":{}}}",
        zero_side_effect, leak, reserve_leak, exhaustion_leak, before, fragmented.used, zero.used);
}
"""

def run(strict=False, proposed_fix=False):
    fragment = extract_mm(MM.read_text())
    if proposed_fix:
        from proposed_fix import apply
        fragment = apply(fragment)
    with tempfile.TemporaryDirectory(prefix="warden-mm-audit-") as d:
        rust_file = Path(d) / "probe.rs"
        executable = Path(d) / "probe"
        rust_file.write_text("#![allow(dead_code)]\n" + fragment + "\n" + HARNESS)
        compiled = subprocess.run(
            ["rustc", "--edition=2021", "-O", str(rust_file), "-o", str(executable)],
            capture_output=True, text=True, timeout=90, check=False)
        if compiled.returncode:
            raise RuntimeError("rustc failed: " + compiled.stderr[-3000:])
        process = subprocess.run([str(executable)], capture_output=True,
                                 text=True, timeout=15, check=False)
        if process.returncode:
            raise RuntimeError("source-backed probe failed: " + process.stderr[-3000:])
    report = json.loads(process.stdout)
    if report["source"] != "actual FrameAlloc from mm.rs":
        raise RuntimeError("wrong probe output")
    report["scope"] = "Linux host extracted-source reproduction only; NOT a native Warden/QEMU test"
    report["issue"] = 160
    report["implementation"] = "proposed_unmerged_patch" if proposed_fix else "repository_baseline"
    print(json.dumps(report, sort_keys=True))
    return 1 if strict and (report["zero_count_has_side_effect"] or
                            report["noncontiguous_failure_leaks_used"] or
                            report["reserved_overlap_failure_leaks_used"] or
                            report["exhaustion_failure_leaks_used"]) else 0

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--strict", action="store_true",
                        help="return failure if allocator defects persist (use after kernel fix)")
    parser.add_argument("--proposed-fix", action="store_true",
                        help="compile proposed safe bump-run implementation instead of the unmodified source")
    args = parser.parse_args()
    try:
        return run(args.strict, args.proposed_fix)
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as err:
        print(json.dumps({"status": "ERROR", "reason": str(err)}))
        return 2

if __name__ == "__main__":
    raise SystemExit(main())
