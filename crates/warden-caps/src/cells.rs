//! Input cells: external input gets its own encoding.
//!
//! Every input byte (or Unicode scalar) is widened to a 32-bit cell in
//! `0..=0x10FFFF`. System control values (end of input, framing, commands)
//! exist only above `0x10FFFF`. No byte sequence can decode to a control
//! value, so input can never forge a terminator or a command.

use alloc::vec::Vec;

/// Highest value an input cell may hold.
pub const INPUT_MAX: u32 = 0x10_FFFF;

/// Control values. All lie strictly above `INPUT_MAX`.
pub const CTRL_END: u32 = 0x11_0000;
pub const CTRL_FRAME: u32 = 0x11_0001;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Cell(u32);

impl Cell {
    /// Widen one input byte. Always lands in the input range.
    pub const fn from_byte(b: u8) -> Cell {
        Cell(b as u32)
    }
    /// Widen a Unicode scalar. `char` is at most 0x10FFFF by construction.
    pub const fn from_char(c: char) -> Cell {
        Cell(c as u32)
    }
    /// Only system code can build a control cell; it takes a control value.
    pub const fn control(v: u32) -> Option<Cell> {
        if v > INPUT_MAX { Some(Cell(v)) } else { None }
    }
    pub const fn is_input(self) -> bool {
        self.0 <= INPUT_MAX
    }
    pub const fn raw(self) -> u32 {
        self.0
    }
}

/// Untrusted input. It can't be used where trusted data is expected; the only
/// way out is `validate`, which runs a caller-supplied checker.
pub struct Untrusted<T>(T);

impl<T> Untrusted<T> {
    pub fn new(v: T) -> Self {
        Untrusted(v)
    }
    pub fn validate<U, E>(self, f: impl FnOnce(T) -> Result<U, E>) -> Result<U, E> {
        f(self.0)
    }
}

/// Widen a byte stream into input cells and append the out-of-band terminator.
pub fn widen(bytes: &[u8], max: usize) -> Option<Vec<Cell>> {
    // Hard ceiling set by the receiver, never by the sender.
    if bytes.len() > max {
        return None;
    }
    let mut v = Vec::with_capacity(bytes.len() + 1);
    v.extend(bytes.iter().map(|&b| Cell::from_byte(b)));
    v.push(Cell(CTRL_END));
    Some(v)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn no_byte_or_char_reaches_control_range() {
        for b in 0..=255u8 {
            assert!(Cell::from_byte(b).is_input());
        }
        // Every Unicode scalar value.
        for v in 0..=INPUT_MAX {
            if let Some(c) = char::from_u32(v) {
                assert!(Cell::from_char(c).is_input());
                assert_ne!(Cell::from_char(c).raw(), CTRL_END);
            }
        }
        assert!(Cell::control(INPUT_MAX).is_none());
        assert!(!Cell::control(CTRL_END).unwrap().is_input());
    }

    #[test]
    fn terminator_only_at_end() {
        let evil = [0x00u8, 0xFF, b'\n', 0x1A, 0x04];
        let cells = widen(&evil, 16).unwrap();
        let ends: Vec<_> = cells.iter().enumerate().filter(|(_, c)| !c.is_input()).collect();
        assert_eq!(ends.len(), 1);
        assert_eq!(ends[0].0, evil.len());
        assert!(widen(&[0u8; 17], 16).is_none());
    }
}
