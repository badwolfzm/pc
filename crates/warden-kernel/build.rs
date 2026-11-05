use std::env;
use std::path::PathBuf;
use std::process::Command;

/// The Phase 2b user programs, built from `user/` with the host C compiler
/// into static, non-PIE ELF images the kernel embeds and loads at boot.
const PROGRAMS: &[&str] = &["personality", "httpd", "fetch", "rogue"];

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

    let cc = env::var("WARDEN_CC").unwrap_or_else(|_| "cc".into());
    let user = dir.join("user");
    println!("cargo:rerun-if-env-changed=WARDEN_CC");
    for entry in std::fs::read_dir(&user).expect("user/ directory") {
        println!("cargo:rerun-if-changed={}", entry.unwrap().path().display());
    }
    for prog in PROGRAMS {
        let elf = out.join(format!("{prog}.elf"));
        // No SSE/x87: the kernel saves only general registers on a trap.
        let st = Command::new(&cc)
            .args([
                "-std=gnu11", "-O2", "-Wall", "-Wextra",
                "-ffreestanding", "-nostdlib", "-nostartfiles", "-static", "-no-pie", "-fno-pie",
                "-fno-stack-protector", "-fno-asynchronous-unwind-tables", "-fno-builtin",
                "-mgeneral-regs-only", "-mno-red-zone",
                "-Wl,--build-id=none", "-Wl,-z,noexecstack", "-Wl,-z,max-page-size=4096", "-s",
            ])
            .arg(format!("-Wl,-T,{}", user.join("app.ld").display()))
            .arg("-o")
            .arg(&elf)
            .arg(user.join(format!("{prog}.c")))
            .arg(user.join("libw.c"))
            .status()
            .unwrap_or_else(|e| panic!("{cc} not found ({e}); set WARDEN_CC to a C compiler"));
        assert!(st.success(), "{cc} failed on user/{prog}.c");
    }
}
