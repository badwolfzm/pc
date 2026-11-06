# Pinned upstream Doom C formatting callsite inventory

Task #261. Review source-backed `printf`, `fprintf`, `sprintf`, `snprintf`, `vprintf`, `vfprintf`, `vsprintf`, and `vsnprintf` callsites in the 79 **actual pinned Doomgeneric engine translation units** used by Warden's native link gate.

This standalone tool independently flags calls to unavailable freestanding stdio ABI functions (`fprintf`, `sprintf`, `vprintf`, `vfprintf`, `vsprintf`) **even if their format strings use supported conversions**. It also flags unsupported format conversions (`%f`, `%e`, `%g`, `%a`, `%p`, `%n` and unknowns), dynamic `*` widths/precision, malformed trailing percent and nonliteral format expressions as manual-review items. It produces a JSON report with source filenames, line numbers and format excerpts. It never edits original engine code or invents runtime support. The present bounded Warden formatter only implements a subset of common integer/string conversions. Review **every** reported callsite with the authoritative upstream source and the exact deployed formatter revision.

The scanner is **intentionally conservative but NOT a full C preprocessor/AST**. It cannot resolve macros, conditional compilation, format wrapper functions, generated source, trigraphs, complex lexical edge cases, compiler optimizations, or actual execution frequency. Its static findings are diagnostic evidence, not a proof of completeness or memory safety; compiling/instrumenting genuine Doom and manual security/runtime review remain mandatory. A zero-warning report does not establish format compatibility.

CI pins original Doomgeneric `e257a09527062bfb986b6e5e0780839b6cbd5b71` and consumes the same upstream Makefile manifest and 79-source filter as `tools/doom-native-link-gate/gate.py`. It runs adversarial scanner tests, inventories real source, and uploads a machine-readable report. This is independent from the global `printf` implementation in candidate PR #258, which needs kernel-side capability review.

No kernel/ABI, formatter provider, WAD/graphics, host I/O, source, PR owner, or rights change; only new `tools/doom-format-callsite-audit/**` and `.github/workflows/doom-format-callsite-audit.yml`. No executable, QEMU boot, Doom game frame, 512 MiB memory observation or Albion compatibility is claimed.

## Review priorities

The verified initial source inventory recorded five dynamic/unparsed formats, one literal `%f` in `m_config.c`, and `%p` calls in the memory diagnostics (`i_system.c`, `z_zone.c`). These are source-level possibilities, not proven runtime paths. The revised report additionally counts all calls that require an unavailable `fprintf`/other stdio implementation. Review flagged calls against the merged capability-checked console adapter and existing Warden WAD bridge; do not translate `FILE*` operations into an unrestricted host stream.
