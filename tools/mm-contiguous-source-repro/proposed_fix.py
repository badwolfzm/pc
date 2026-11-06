"""Source-level proposed fix for Warden FrameAlloc::alloc_contiguous.

Uses a fresh contiguous bump allocation rather than consuming fragmented free-list
entries. This is a handoff candidate, not an accepted kernel change.
"""
START = "    pub fn alloc_contiguous(&mut self, n: u64) -> Option<u64> {"
END = "\n    }\n}"

REPLACEMENT = r"""    pub fn alloc_contiguous(&mut self, n: u64) -> Option<u64> {
        if n == 0 {
            return None;
        }
        let bytes = n.checked_mul(PAGE)?;
        let next_used = self.used.checked_add(n)?;
        let mut first = self.next;
        let mut last = first.checked_add(bytes)?;
        // Reject overlap with the boot-module reservation, even if the
        // requested range *starts before* the reserved range.
        if first < self.skip.1 && last > self.skip.0 {
            first = self.skip.1;
            last = first.checked_add(bytes)?;
        }
        if last > self.end {
            return None;
        }
        // Commit only after all arithmetic and reservation checks have passed.
        self.next = last;
        self.used = next_used;
        unsafe { ptr::write_bytes(first as *mut u8, 0, bytes as usize) };
        Some(first)
    }"""

def apply(source):
    if source.count(START) != 1 or source.count(END) != 1:
        raise ValueError("allocator function shape changed; do not patch automatically")
    before, remainder = source.split(START, 1)
    old, after = remainder.split(END, 1)
    if "for i in 1..n" not in old or "let first = self.alloc()?" not in old:
        raise ValueError("baseline is no longer the known vulnerable implementation")
    return before + REPLACEMENT + END[len("\n    }"):] + after
