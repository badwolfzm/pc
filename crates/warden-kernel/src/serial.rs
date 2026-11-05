//! COM1 serial console. The only output device in Phase 1; polled, no IRQ.

use core::fmt;

const COM1: u16 = 0x3F8;

pub unsafe fn outb(port: u16, v: u8) {
    core::arch::asm!("out dx, al", in("dx") port, in("al") v, options(nomem, nostack, preserves_flags));
}
pub unsafe fn inb(port: u16) -> u8 {
    let v: u8;
    core::arch::asm!("in al, dx", in("dx") port, out("al") v, options(nomem, nostack, preserves_flags));
    v
}

pub fn init() {
    unsafe {
        outb(COM1 + 1, 0x00); // no interrupts
        outb(COM1 + 3, 0x80); // DLAB
        outb(COM1, 0x01); // 115200
        outb(COM1 + 1, 0x00);
        outb(COM1 + 3, 0x03); // 8N1
        outb(COM1 + 2, 0xC7); // FIFO
        outb(COM1 + 4, 0x03); // RTS/DSR, no IRQ
    }
}

pub struct Serial;

impl fmt::Write for Serial {
    fn write_str(&mut self, s: &str) -> fmt::Result {
        for b in s.bytes() {
            unsafe {
                while inb(COM1 + 5) & 0x20 == 0 {}
                outb(COM1, b);
            }
        }
        Ok(())
    }
}

#[macro_export]
macro_rules! kprint {
    ($($arg:tt)*) => {{ use core::fmt::Write; let _ = write!($crate::serial::Serial, $($arg)*); }};
}
#[macro_export]
macro_rules! kprintln {
    () => { $crate::kprint!("\n") };
    ($($arg:tt)*) => {{ $crate::kprint!($($arg)*); $crate::kprint!("\n"); }};
}
