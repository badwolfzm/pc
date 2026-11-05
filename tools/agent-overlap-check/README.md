# Agent path overlap checker

Task: [#41](https://github.com/badwolfzm/pc/issues/41). This is an **offline advisory** tool, not an ownership ledger, lock, merge gate, or substitute for reading live issues and PR changed-file lists. Follow [control/COORDINATION.md](../../control/COORDINATION.md).

Record each agent's declared paths in a local JSON file:

```json
{
  "agents": [
    {"id": "net-agent", "paths": ["crates/network/**"]},
    {"id": "abi-agent", "paths": ["crates/warden-kernel/src/linux.rs"]},
    {"id": "review-agent", "paths": ["tools/agent-overlap-check/**"]}
  ]
}
```

```sh
python3 tools/agent-overlap-check/check.py claims.json
python3 -m unittest discover -s tools/agent-overlap-check -p 'test_*.py'
```

Exit codes: **0** no overlap among supplied claims, **1** detected overlap, **2** invalid or ambiguous input. The tool accepts exact repository-relative paths or entire subtrees using **one trailing `/**`**. An exact path means that exact file; it does not imply descendants. Patterns such as `src/*.rs`, `**`, traversal, absolute paths, and Windows separators are rejected rather than guessed.

**Important limitations:** Only claims *you provide* are checked. This cannot discover hidden edits, open PRs, branch changes, stale claims, ownership outside the JSON, or simultaneous newly created claims. A zero exit code is **not proof** that edits are safe. Re-check live GitHub state and coordinate with the owner before modifying shared files. No network access, kernel changes or runtime security claims.

## Handoff

- Owner: ChatGPT GitHub coordination session
- Base: `70bc55e77a588561ded1ffaaace067283608e626`
- Branch: `agent/overlap-check-20261105`
- Scope: exclusively `tools/agent-overlap-check/**`
- Dependencies: none, independent of open kernel and ABI PRs
- Review: independent verification required before merge
- Follow-up: lead/bookkeeping may choose to link this tool from their own coordination docs; this PR does not edit them
