# codegen golden: signed guest (frozen 2026-11-05, contract 2.0.4)

Inputs are the raw page images, not the ELF, so the rebuild does not depend on
the clang version. `guest_text.bin`/`guest_data.bin` were extracted once from a
`clang -O2 -static-pie -nostdlib` build of `matrixos/codegen/tests/run_mxsb.sh`'s
guest (text 12288 B incl. page padding, data 4096 B, no BSS, entry_rel 0x1000).
`key.pem` is a TEST key with no trust value; `pub.pem` is its public half.

Rebuild (byte-identical; Ed25519 signatures are deterministic):

    mxsb_emit build --text guest_text.bin --data guest_data.bin --entry 0x1000 \
      --manifest manifest.txt --superop blit --tier multi \
      --build-info golden=codegen-1 --sign key.pem -o guest.mxsb

`guest.mxsb.sha256` is the expected SHA-256 of the file, `guest.dump.txt` the
expected `mxsb_emit dump` text. `codegen/tests/run_mxsb.sh` rebuilds and
asserts all three plus `../MANIFEST.sha256`.
