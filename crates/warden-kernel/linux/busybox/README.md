# BusyBox under the Linux personality

`busybox` is an unmodified BusyBox 1.36.1, statically linked against musl
(Ubuntu 24.04 `musl-tools`, musl 1.2.4), non-PIE, stripped, 158 KiB, with
the applets echo, cat, sh (ash with the printf/echo/test builtins and
`$((...))`), uname, head, wc, printf, true and false. `build.sh` rebuilds it
from the upstream tarball with `config`; it is checked in so the kernel
build needs no musl toolchain and no network.

- source: busybox-1.36.1.tar.bz2, sha256
  `b8cc24c9574d809e7279c3be349795c5d5ceb6fdf19ca709f80cde50e47de314`
- binary: sha256
  `d4a2bd10937b548405a511318278893e873f5d13fdf29a73dac340e261638aa8`

The kernel starts it four times (`src/linux.rs`, `APPS`) with different
argv and grants, and judges the exact bytes each run writes.
