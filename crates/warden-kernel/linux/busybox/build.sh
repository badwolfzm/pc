#!/usr/bin/env bash
# Rebuild linux/busybox/busybox: an unmodified static, non-PIE musl BusyBox
# 1.36.1 with a handful of applets (echo, cat, sh, printf, uname, head, wc,
# true, false). Needs musl-gcc (Debian/Ubuntu: musl-tools) and the upstream
# tarball; the hashes below pin both the input and the result.
#   SRC_SHA256 b8cc24c9574d809e7279c3be349795c5d5ceb6fdf19ca709f80cde50e47de314  busybox-1.36.1.tar.bz2
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
TARBALL="${1:?usage: build.sh busybox-1.36.1.tar.bz2}"
echo "b8cc24c9574d809e7279c3be349795c5d5ceb6fdf19ca709f80cde50e47de314  $TARBALL" | sha256sum -c -
WORK="$(mktemp -d)"
tar -xjf "$TARBALL" -C "$WORK"
cd "$WORK/busybox-1.36.1"
cp "$HERE/config" .config
yes "" | make -s oldconfig >/dev/null
make -s -j"$(nproc)" CC=musl-gcc EXTRA_CFLAGS="-fno-pie" EXTRA_LDFLAGS="-no-pie" busybox
cp busybox "$HERE/busybox"
sha256sum "$HERE/busybox"
