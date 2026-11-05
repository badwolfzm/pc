# MXSB v2 reference loader (`mxsb-ref/`)

Owner: thread "Upgrade MXSB manifest format" (contract owner). Contract:
`../mxsb-manifest.md` 2.0.4, rules V1..V28 in section 9a, error codes from
`../mxsb-manifest.schema.json`.

This is the implementation the codegen emitter must pass, the tests thread
wires finding F7 to, and the kernel ports to Rust in Phase 3. It is written
independently of `matrixos/codegen/mxsb/` (own SHA-256, own manifest parser,
own bounds-checked reader) so the two can be compared on the same bytes.

| file | what |
|---|---|
| `mxsb_load.h` / `mxsb_load.c` | the loader check: `mxsb_ld_check(buf, len, policy, image, diag)`. No allocation, no I/O, no libc beyond `string.h`/`snprintf`. Also the `[grants]` subset parser for `warden.toml` and SHA-256. |
| `mxsb_check.c` | CLI: `mxsb_check FILE --container warden.toml [--approve HEX]... [--pubkey PUB.pem] [--structural] [--digest] [--dump]`. Exit 0 admitted, 1 refused (prints `<error> <rule>: <detail>`), 2 usage. Ed25519 through `/usr/bin/openssl` (fork/exec with an argv, no shell). |
| `mxsb_corpus.c` | builds the corpus: 2 valid files from a minimal independent emitter, plus one mutant per rule (66 files) and `expected.txt`. |
| `fixtures/*.toml` | container manifests: the full fixture, one without `allow_unsigned`, six restricted ones for V26. |
| `run_tests.sh` | builds everything with gcc (ASan+UBSan, `-Werror`) and clang, runs the corpus, a 20k random-mutation fuzz, and the cross-check against the codegen emitter. |

Run with `bash run_tests.sh [BUILD_DIR] [CODEGEN_DIR]` (the project-files
mount is noexec; build products go to BUILD_DIR, default `/tmp/mxsb-ref-build`).

## Loader behaviour worth knowing

- Order: V1..V13, V27, V28, V14..V26, stop at the first failure. The image is
  zeroed on any failure. The loader allocates nothing; `image.mapped_total`
  (TEXT+DATA+BSS page-rounded, at most 4 GiB) is the one allocation the
  caller makes afterwards.
- V20 outcomes: unsigned (96 zero bytes) and the container lacks
  `allow_unsigned` = `MXSB_E_SIGNATURE`; unsigned, allowed, digest not on the
  approvals list = `MXSB_E_UNTRUSTED_KEY`; signed with a key id outside the
  trust list = `MXSB_E_UNTRUSTED_KEY`; signed by a trusted key but the
  signature does not verify = `MXSB_E_SIGNATURE`. Ed25519 is a callback
  (`policy.verify`); the core has no public-key code so it can move into the
  kernel unchanged.
- V26 attenuation: path grants by prefix (`/app` covers `/app/assets`),
  `net.connect` exact or against a container-side `*` host or port,
  `net.listen` exact, `superop=` must be listed in `superops`, `render` and
  `random` must be written `= true` (no default), `clock` precise covers
  coarse, `memory.max` and `cpu.max_cores` at or under the container budget.
  `load` is always granted to a binary.
- Section types 8..0x7FFF are skipped (reserved for later minors); type 0 is
  refused (V10, contract 2.0.4). SUPEROPS is capped at 256 ids by this loader.

## Results (2026-11-05, this container: gcc 13.3, clang 18, openssl 3)

```
passed 207, failed 0, known divergences from codegen 1
```

Measured, by running `bash run_tests.sh`:

- SHA-256 NIST vectors (`abc`, one million `a`): pass.
- Core object references no `malloc`/`calloc`/`free`/`fopen`/`read`/`mmap`: pass.
- 68 corpus files, each checked by the gcc ASan/UBSan build and the clang
  build with identical results; every rule V1..V28 has at least one case
  that produces its named error (V18 has only a positive case: a failing one
  needs more than 4 GiB of file and cannot be written here). Positives:
  `valid_unsigned`, `valid_minimal`, `v10_future_type_skipped`,
  `v18_bss_1gib_ok`.
- 20,000 random mutations of the valid file under ASan/UBSan: no crash, no
  sanitizer report, every refusal names a rule (201 admitted, 19,799 refused).
- Cross-check with the codegen emitter (`mxsb_emit`, built from
  `matrixos/codegen/mxsb` at 2.0.3): its verifier agrees with this corpus on
  every structural case except one (below); a file it emits unsigned is
  admitted here on the approvals path with the same digest both sides compute;
  a file it signs with Ed25519 is admitted with `--pubkey`, refused as
  `MXSB_E_UNTRUSTED_KEY` under a different key or an empty trust list, and
  refused as `MXSB_E_SIGNATURE` after one signature bit is flipped.

Known divergence, reported to the codegen thread: `v10_type_zero` (section
type 0). This loader refuses it under V10; the codegen validator skips it as
an unknown type. Contract 2.0.4 makes type 0 a refusal.

Not verified: behaviour on big-endian or 32-bit hosts (the reader is
endian-explicit but was only run on x86-64); the loader has not mapped or
executed anything (it checks, the kernel maps); no golden `.mxsb` files are
frozen yet. Freezing the first golden set, from this corpus plus one
emitter-built signed file, is the next step and should be done together with
the codegen thread so both suites pin the same bytes.
