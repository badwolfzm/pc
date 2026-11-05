//! Check tests/vectors/cells.vec against the kernel's Rust cell model.
//!
//! The kernel file is included at build time (build.rs strips only its `//!`
//! doc comments, which include! cannot take) from the path in the
//! WARDEN_CELLS_RS environment variable (default: the Phase 0 tarball layout),
//! so this binary tests the real kernel code, not a copy.
//!
//!   WARDEN_CELLS_RS=/path/to/kernel/crates/warden-caps/src/cells.rs \
//!     cargo run --release -- ../cells.vec
extern crate alloc;

#[allow(dead_code)]
mod cells {
    include!(env!("WARDEN_CELLS_INCLUDED"));
}
use cells::{widen, Cell, CTRL_END, INPUT_MAX};

fn hex(s: &str) -> u32 { u32::from_str_radix(s, 16).expect("hex") }
fn bytes(s: &str) -> Vec<u8> {
    if s == "-" { return Vec::new(); }
    (0..s.len()).step_by(2).map(|i| u8::from_str_radix(&s[i..i + 2], 16).expect("hex byte")).collect()
}

/// UTF-8 widening in Rust terms: the standard validator, then Cell::from_char per scalar.
fn widen_utf8(b: &[u8], max: usize) -> Result<Vec<Cell>, (&'static str, usize)> {
    if b.len() > max { return Err(("budget", 0)); }
    match core::str::from_utf8(b) {
        Ok(s) => {
            let mut v: Vec<Cell> = s.chars().map(Cell::from_char).collect();
            v.push(Cell::control(CTRL_END).unwrap());
            Ok(v)
        }
        Err(e) => Err(("encoding", e.valid_up_to())),
    }
}

fn cells_eq(got: &[Cell], want: &[u32]) -> bool {
    got.len() == want.len() && got.iter().zip(want).all(|(c, w)| c.raw() == *w)
        && got[..got.len() - 1].iter().all(|c| c.is_input()) && !got[got.len() - 1].is_input()
}

fn main() {
    let path = std::env::args().nth(1).unwrap_or_else(|| "../cells.vec".into());
    let text = std::fs::read_to_string(&path).expect("read cells.vec");
    let (mut n, mut fails) = (0, 0);
    for (i, raw) in text.lines().enumerate() {
        let line = raw.split('#').next().unwrap().trim();
        if line.is_empty() { continue; }
        n += 1;
        let (lhs, rhs) = line.split_once("->").expect("arrow");
        let l: Vec<&str> = lhs.split_whitespace().collect();
        let r: Vec<&str> = rhs.split_whitespace().collect();
        let mut fail = |msg: String| { eprintln!("vec line {}: {}", i + 1, msg); fails += 1; };
        match l[0] {
            "byte" => {
                let c = Cell::from_byte(hex(l[1]) as u8);
                if r[0] != "cell" || c.raw() != hex(r[1]) || !c.is_input() { fail(format!("byte -> {:x}", c.raw())); }
            }
            "scalar" => {
                let got = char::from_u32(hex(l[1])).map(Cell::from_char);
                match (r[0], got) {
                    ("ok", Some(c)) if c.raw() == hex(r[1]) && c.is_input() => {}
                    ("reject", None) => {}
                    _ => fail(format!("scalar {:?}", got.map(|c| c.raw()))),
                }
            }
            "control" => {
                let got = Cell::control(hex(l[1]));
                match (r[0], got) {
                    ("ok", Some(c)) if !c.is_input() => {}
                    ("reject", None) => {}
                    _ => fail(format!("control {:?}", got.map(|c| c.raw()))),
                }
            }
            "widen" => {
                let max: usize = l[2][4..].parse().unwrap();
                let got = widen(&bytes(l[1]), max);
                match (r[0], got) {
                    ("ok", Some(v)) => {
                        let want: Vec<u32> = r[1..].iter().map(|s| hex(s)).collect();
                        if !cells_eq(&v, &want) { fail(format!("widen cells {:x?}", v.iter().map(|c| c.raw()).collect::<Vec<_>>())); }
                    }
                    ("budget", None) => {}
                    (w, g) => fail(format!("widen want {} got {:?}", w, g.map(|v| v.len()))),
                }
            }
            "utf8" => {
                let max: usize = l[2][4..].parse().unwrap();
                match (r[0], widen_utf8(&bytes(l[1]), max)) {
                    ("ok", Ok(v)) => {
                        let want: Vec<u32> = r[1..].iter().map(|s| hex(s)).collect();
                        if !cells_eq(&v, &want) { fail(format!("utf8 cells {:x?}", v.iter().map(|c| c.raw()).collect::<Vec<_>>())); }
                    }
                    ("budget", Err(("budget", _))) => {}
                    ("encoding", Err(("encoding", consumed))) => {
                        if consumed != r[1].parse::<usize>().unwrap() { fail(format!("consumed {}", consumed)); }
                    }
                    (w, g) => fail(format!("utf8 want {} got {:?}", w, g.map(|v| v.len()))),
                }
            }
            op => fail(format!("unknown op {}", op)),
        }
    }
    let _ = INPUT_MAX;
    println!("cells.vec (Rust): {} vectors, {} failures", n, fails);
    std::process::exit(if fails == 0 { 0 } else { 1 });
}
