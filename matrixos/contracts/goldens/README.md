# MXSB golden set (frozen 2026-11-05, contract 2.0.4)

Every byte here is pinned by `MANIFEST.sha256`. Changing a golden file is a
reviewed contract change with a changelog entry; from this freeze on, the
version policy in `../mxsb-manifest.md` section 7 applies strictly.

- `ref/`      68 files from the reference corpus (`../mxsb-ref/mxsb_corpus.c`,
              deterministic output) plus `expected.txt` with `<name> <error> <rule>`.
- `codegen/`  the codegen thread's deterministically built, Ed25519-signed
              guest from `matrixos/codegen/tests/guest/` with its expected
              SHA-256 and dump text (dropped in by that thread).

Both suites assert the hashes: `mxsb-ref/run_tests.sh` regenerates the corpus
and compares it byte for byte against `ref/`, then re-checks `MANIFEST.sha256`
over the whole directory; `codegen/tests/run_mxsb.sh` does the same for its file.

Regenerate the manifest only as part of a deliberate change:
`cd goldens && find . -type f ! -name MANIFEST.sha256 ! -name README.md | sort | xargs sha256sum > MANIFEST.sha256`
