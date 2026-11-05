# Handoff: Linux ABI reference negative cases

- Task: [#35](https://github.com/badwolfzm/pc/issues/35).
- Owner: Codex coordination/review session.
- Base: `8174e7d0d2af5aa07a14a9c8902736b917c61979`.
- Owned paths: `tools/linux-abi-reference/**` only.
- Dependencies: none. Related follow-up for #26/#32; no kernel edits.
- Behavior: native Linux reference queries emit machine-readable results;
  test harness checks all 16 cases and verifies failure propagation by
  deliberately substituting an incorrect expected errno in a temporary copy.
- Validation: Linux 6.18.57 x86-64, GCC 13.3.0, Python 3.12.14; three Python
  tests pass, 16 native cases pass; wrong-error mutation returns exit 1 with
  failed cases. Compiler warnings are errors.
- Initial test-first check: both tests failed because probe.c was absent;
  after implementation and correcting a libc helper-name conflict, both passed.
  Independent review found inconsistent per-case sentinel failure reporting;
  fixed it and added a third mutation regression, verified red/green.
- Not verified: Warden guest execution, target PC/WSL2, other architectures,
  musl, restricted-host syscall policies. Rust/QEMU suites were not run here.
- Integration request: Linux ABI owner should port the relevant scenarios
  into its guest corpus and retain stronger all-or-nothing user-copy tests.
- Suggested title: `test: add native Linux ABI negative-case reference probes`.
