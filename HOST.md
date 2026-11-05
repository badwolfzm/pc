# Running Warden on the target host (Windows PC)

The target host is a Windows PC. Everything here runs under **WSL2**
(Ubuntu 24.04 or similar). Nothing in this file has been verified on Windows
yet; it has been verified on a Linux 6.18 x86-64 cloud VM without KVM. The
"Not verified" list at the end says what still needs a real run.

## One-time setup (inside WSL2)

```sh
sudo apt-get install -y build-essential nasm qemu-system-x86 binutils
# build-essential also provides the `cc` the kernel build uses to compile the
# Phase 2b user programs (crates/warden-kernel/user/*.c) and the Linux test
# programs (crates/warden-kernel/linux/*.c); set WARDEN_CC to
# point at another compiler if `cc` is not gcc or clang.
curl https://sh.rustup.rs -sSf | sh          # stable toolchain
rustup target add x86_64-unknown-none        # bare-metal target for the kernel
git clone https://github.com/badwolfzm/pc && cd pc
cargo build --release                        # Phase 0 host tools
cargo build --release -p warden-kernel --target x86_64-unknown-none
```

## Phase 0 (host supervisor) under WSL2

`warden run` needs these kernel features, all present in the WSL2 kernel
(5.15+): seccomp user notification with `ADDFD`, `openat2`, `pidfd_getfd`.

```sh
cargo test                 # 41 unit tests
tests/escape/run.sh        # 67 attack checks, run as root inside the sandbox
tests/compat/run.sh        # 14 checks with ls, sh, tar, python3 ...
```

If `warden run` fails with `could not install seccomp filter`, the WSL2
kernel was built without `CONFIG_SECCOMP_FILTER`; update WSL
(`wsl --update`) which ships a kernel that has it.

## Phase 1 (kernel) under QEMU

```sh
tests/kernel/run.sh        # builds, boots in QEMU, runs 191 in-guest checks
```

The script picks **KVM** when `/dev/kvm` is writable and falls back to
**TCG** (pure emulation) otherwise. The suite takes about 2 s under TCG, so
emulation is fine for development.

Nested KVM in WSL2: Windows 11 with a recent WSL exposes `/dev/kvm` when
`nestedVirtualization=true` is set in `%UserProfile%\.wslconfig`:

```
[wsl2]
nestedVirtualization=true
```

Then `sudo chmod 666 /dev/kvm` (or add your user to the `kvm` group) and
rerun the script.

Fallback, QEMU on Windows itself with WHPX (Hyper-V acceleration):
install QEMU for Windows, enable the "Windows Hypervisor Platform" feature,
copy `target/x86_64-unknown-none/release/warden-kernel.mb32` out of WSL and
run

```
qemu-system-x86_64.exe -machine pc,accel=whpx -m 128M -display none -serial stdio ^
  -no-reboot -device isa-debug-exit,iobase=0xf4,iosize=0x04 -kernel warden-kernel.mb32
```

Exit status 1 means every check held (QEMU reports `(code << 1) | 1`).

## Not verified

- Any of the above on Windows or WSL2. Verified only on a Linux cloud VM
  (Ubuntu 24.04 userland, kernel 6.18, no `/dev/kvm`, QEMU 8.2 TCG).
- Nested KVM performance and the WHPX fallback.
- The host hardening checks from the design doc (IOMMU strict mode, SMT off,
  TSX off, microcode) are not implemented yet; `warden-run` will refuse to
  start when they fail, in Phase 3.
