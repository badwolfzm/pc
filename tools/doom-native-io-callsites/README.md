# Genuine Doom unsafe-host-I/O source call-site audit

A read-only lexical inventory for the **unchanged** pinned Doomgeneric engine revision `e257a09527062bfb986b6e5e0780839b6cbd5b71`, triggered by the ~32+ unresolved C runtime imports in merged #170 and draft #176/#181. The audit enumerates function-call candidates for `system`, `fopen`, `fclose`, `fread`, `fwrite`, `fseek`, `ftell`, `remove`, `rename`, `mkdir` in the **79 true non-platform/non-stdio-backend engine C translation units**. It strips C comments and string/char literals while preserving line references, saves hashes of original lines (not code excerpts) and a per-function count. This is complementary to #170's exact unresolved-object provenance; it identifies specific source locations to review.

**Never assume each lexical call is reachable**, or that every unresolved symbol has a direct call (macro expansions, pointers and compiler lowering may differ). This is not a full C parser or security proof; source revision pinning is enforced, host APIs are not implemented or silently stubbed, and no Warden permissions/kernel policies change. The next responsible integration step is to classify each call by runtime path (IWAD read, save/config write, shell invocation, etc.), design capability-mediated Warden substitutes for authorized storage and reject unsupported dynamic OS actions. Prevent unsanctioned writeback and command execution.

```sh
python3 -m unittest discover -s tools/doom-native-io-callsites -p 'test_*.py' -v
python3 tools/doom-native-io-callsites/audit.py --source /path/to/pinned/doomgeneric --output /tmp/doom-native-io-audit.json
```

Only owns `tools/doom-native-io-callsites/**` and `.github/workflows/doom-native-io-callsites.yml`; no kernel, engine, native loader, allocator, WAD or other contributor paths changed. This is integration diagnosis, not executable linking or Warden/QEMU Doom boot, 512 MiB RAM or FPS proof.
