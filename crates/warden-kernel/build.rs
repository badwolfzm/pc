use std::env;
use std::path::PathBuf;
use std::process::Command;

fn main() {
    let out = PathBuf::from(env::var("OUT_DIR").unwrap());
    let dir = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
    if env::var("CARGO_CFG_TARGET_OS").unwrap() != "none" {
        return;
    }
    let obj = out.join("boot.o");
    let st = Command::new("nasm")
        .args(["-f", "elf64", "-o"])
        .arg(&obj)
        .arg(dir.join("src/boot.asm"))
        .status()
        .expect("nasm not found");
    assert!(st.success(), "nasm failed");
    println!("cargo:rerun-if-changed=src/boot.asm");
    println!("cargo:rerun-if-changed=linker.ld");
    println!("cargo:rustc-link-arg={}", obj.display());
    println!("cargo:rustc-link-arg=-T{}", dir.join("linker.ld").display());
    println!("cargo:rustc-link-arg=-static");
    println!("cargo:rustc-link-arg=--no-dynamic-linker");
}
