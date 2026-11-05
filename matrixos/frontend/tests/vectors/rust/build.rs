// Copy the kernel's cells.rs into OUT_DIR with its inner doc comments (`//!`)
// stripped, because `include!` cannot take a file that starts with them.
// Nothing else is changed: the code under test is the kernel's own.
use std::{env, fs, path::Path};
fn main() {
    let src = env::var("WARDEN_CELLS_RS").expect("set WARDEN_CELLS_RS to kernel/crates/warden-caps/src/cells.rs");
    println!("cargo:rerun-if-env-changed=WARDEN_CELLS_RS");
    println!("cargo:rerun-if-changed={}", src);
    let text = fs::read_to_string(&src).expect("read cells.rs");
    let stripped: String = text.lines().filter(|l| !l.trim_start().starts_with("//!")).map(|l| format!("{}\n", l)).collect();
    let out = Path::new(&env::var("OUT_DIR").unwrap()).join("cells_included.rs");
    fs::write(&out, stripped).unwrap();
    println!("cargo:rustc-env=WARDEN_CELLS_INCLUDED={}", out.display());
}
