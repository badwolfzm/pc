# MXSB v2: super-binary header and manifest contract

Status: draft 2.0.3, 2026-11-05. Owner: thread "Upgrade MXSB manifest format".
Machine-readable companion: `mxsb-manifest.schema.json` (same folder). The
schema is authoritative for constants and names; this file explains them.

Nothing in this contract has been exercised by running code yet. The
prototype's only MXSB test checks the 4-byte magic sniff.

## 1. Scope

MXSB is the native application format for Warden containers. The loader
(host runtime in Phase 0, kernel init service in Phase 1) admits an MXSB
file only when:

1. its bytes are intact (SHA-256 digest matches, signature valid or digest
   approved),
2. every grant it requires is an attenuation of a grant in the container
   manifest (`warden.toml`),
3. every offset and size is inside the file.

Capabilities are then minted exactly as required, never wider.

## 2. Baseline (v1, MatrixOS v9 compact prototype)

```c
#pragma pack(push,1)
typedef struct {
    uint8_t  magic[4];                 // "MXSB"
    uint16_t version, flags;           // version = 1
    uint32_t text_size, data_size, superop_count, caps_required;
    uint64_t entry_off, manifest_hash; // FNV-1a 64 over text+data
} mxsb_header;                         // 40 bytes packed
#pragma pack(pop)
```

v1 is demo output. The loader refuses it (section 7). Tools still read it.

## 3. v2 file layout

All integers little-endian. Encoders and decoders read and write field by
field; no struct copies, no pointer casts into the buffer.

```
file := header(64) pad* section_table pad* section*
```

### 3.1 Header (64 bytes)

| off | size | field | rule |
|---|---|---|---|
| 0 | 4 | `magic` | `"MXSB"` (0x4D 0x58 0x53 0x42) |
| 4 | 1 | `ver_major` | 2. Reader refuses any other value. |
| 5 | 1 | `ver_minor` | 0. Reader accepts any value; unknown sections and flags defined in later minors are ignored only where section 8 says so. |
| 6 | 2 | `header_size` | >= 64. Reader skips bytes 64..header_size. |
| 8 | 4 | `flags` | bit 0 PIE, bit 1 deterministic-only, bit 2 debug sections present. Bits 3..31 must be zero in 2.0. |
| 12 | 4 | `section_count` | 1..64 |
| 16 | 8 | `section_table_off` | >= header_size, multiple of 16 |
| 24 | 8 | `entry_off` | file offset inside the TEXT section |
| 32 | 8 | `caps_summary` | OR of family bits (section 5). Pre-filter only. |
| 40 | 8 | `file_size` | exact length of the file |
| 48 | 16 | `reserved` | zero |

### 3.2 Section table entry (32 bytes each, sorted by `offset`)

| off | size | field |
|---|---|---|
| 0 | 4 | `type` (section 3.3) |
| 4 | 4 | `flags` (bit 0 R, bit 1 W, bit 2 X for TEXT/DATA/BSS; zero otherwise) |
| 8 | 8 | `offset` (file offset, multiple of 16; 0 for BSS) |
| 16 | 8 | `size` |
| 24 | 8 | `reserved` (zero) |

Constraints: no two sections overlap; `offset + size <= file_size` with no
u64 overflow; each unique type appears at most once; required types appear.

### 3.3 Section types

| name | id | required | unique | content |
|---|---|---|---|---|
| MANIFEST | 1 | yes | yes | UTF-8 manifest text (section 4) |
| TEXT | 2 | yes | yes | code, flags R+X |
| DATA | 3 | no | yes | initialised data, flags R+W |
| BSS | 4 | no | yes | `size` only, `offset` = 0 |
| SUPEROPS | 5 | no | yes | array of u32 super-op ids the binary calls |
| SIGNATURE | 6 | yes | yes | 32-byte key id + 64-byte Ed25519 signature |
| BUILD_INFO | 7 | no | yes | UTF-8 `key=value` lines: `profile`, `simd_tier`, `source_sha256`, `toolchain` |
| vendor | >= 0x8000 | no | no | skipped by the reader, still covered by the digest |

### 3.4 Load layout (no vaddr fields)

The section table carries file offsets only. The image layout in memory is
fixed by rule, so emitter and loader need no load addresses:

- `base` is the load address the loader chooses (any page-aligned address;
  with flag PIE the code must tolerate any base).
- TEXT is mapped at `base`. `TEXT.size` is a multiple of 4096 whenever DATA
  or BSS exists, so the permission boundary falls on a page.
- DATA is mapped at `base + TEXT.size`; BSS follows DATA at
  `base + TEXT.size + DATA.size`, zero-filled, and `BSS.size` is rounded up
  to a page by the loader.
- `entry_off` is a file offset inside TEXT; the entry address is
  `base + (entry_off - TEXT.offset)`.
- An emitter converting from ELF uses: TEXT image =
  `[page_floor(lowest vaddr), page_floor(first writable vaddr))`, DATA = the
  writable segment image, BSS = its memsz minus filesz,
  `entry_off = TEXT.offset + (e_entry - page_floor(lowest vaddr))`.
  Vendor sections may hold relocation data; the 2.0 loader applies none.

## 4. MANIFEST section grammar

Line-oriented UTF-8, `\n` line ends, `#` starts a comment, blank lines
ignored. Fields separated by one or more spaces or tabs.

```
mxsb-manifest 1
name      <token>                 # [A-Za-z0-9._-]{1,64}
abi       native | linux-compat
build_id  <64 lowercase hex>      # SHA-256(TEXT bytes || DATA bytes)
require   <perm>[=<scope>]        # zero or more, one per line
```

Permission names come from the kernel ABI contract
`kernel/abi/warden-abi.json` (`warden-abi` schema 0.1), extended by three
names this contract adds. The combined list is in the schema file.

| perm | scope | source |
|---|---|---|
| `load` | none | warden-abi 0.1 |
| `fs.read` | absolute path prefix | warden-abi 0.1 |
| `fs.write` | absolute path prefix | warden-abi 0.1 |
| `exec` | absolute path prefix | warden-abi 0.1 |
| `net.connect` | `host:port`, `*` allowed only in the container manifest | warden-abi 0.1 |
| `net.listen` | `host:port` | warden-abi 0.1 |
| `proc.spawn` | none | warden-abi 0.1 |
| `render` | none | added here; the GPU service capability that replaces `MX_CAP_GPU` |
| `superop` | super-op name from the GPU command-set contract (scope required) | warden-abi 0.2; replaces `MX_CAP_SUPEROP` |
| `clock` | exactly `coarse` or `precise` | warden-abi 0.2; replaces `MX_CAP_CLOCK` |
| `random` | none | warden-abi 0.2; replaces `MX_CAP_RANDOM` |
| `memory.max` | size with K/M/G suffix | budget, not a capability |
| `cpu.max_cores` | integer | budget, not a capability |

Normalisation before hashing and comparison: trim each line, collapse
internal whitespace to one space, drop comments and blanks, sort `require`
lines bytewise, drop exact duplicates. Two manifests with the same grants
produce the same bytes.

An unknown `perm`, an unknown `mxsb-manifest` number, a relative path for a
path-scoped perm, a missing port, or a `build_id` that does not match the
sections is a refusal.

## 5. `caps_summary` family bits (replaces `caps_required`)

`caps_summary` is derived from the MANIFEST and must equal the OR of the
bits below; a disagreement is a refusal. The loader never grants from it.

| bit | family | v1 `caps_required` flag | manifest lines that set it |
|---|---|---|---|
| 0 | fs.read | `MX_CAP_FS_READ` (1<<0) | `require fs.read=...` |
| 1 | fs.write | `MX_CAP_FS_WRITE` (1<<1) | `require fs.write=...` |
| 2 | net | `MX_CAP_NET` (1<<2) | `require net.connect=...`, `require net.listen=...` |
| 3 | render | `MX_CAP_GPU` (1<<3) | `require render` |
| 4 | superop | `MX_CAP_SUPEROP` (1<<4) | `require superop=...` |
| 5 | clock | `MX_CAP_CLOCK` (1<<5) | `require clock=...` |
| 6 | random | `MX_CAP_RANDOM` (1<<6) | `require random` |
| 7 | exec | (none in v1) | `require exec=...` |
| 8 | proc.spawn | (none in v1) | `require proc.spawn` |
| 9 | load | (none in v1) | `require load` |
| 10..63 | reserved | | must be zero |

Bits 0..6 keep their v1 positions so a v1 mask converts by zero-extension.

## 6. Digest and signature (replaces `manifest_hash`)

```
digest = SHA-256( file[0..len] with the SIGNATURE section's bytes removed )
```

That is: every byte of the file in file order, including the header, the
section table, all sections, vendor sections and the padding between them,
except the `SIGNATURE.size` bytes at `SIGNATURE.offset`. Padding bytes
must be zero (V27), so there is no unauthenticated or writable slack
anywhere in the file. BSS contributes nothing (no bytes).

The header is inside the digest, so `caps_summary`, `entry_off` and `flags`
cannot change after signing.

SIGNATURE = `key_id[32] || sig[64]` where `sig = Ed25519_sign(digest)` and
`key_id = SHA-256(public key)`. The loader holds a trust list of key ids.

Admission rule:

1. signature valid under a trusted key id, or
2. `allow_unsigned = true` in the container manifest and `digest` is on the
   approvals list (same binding Phase 0 uses for libraries: one SHA-256 per
   approved object).

Otherwise refuse. FNV-1a is not used anywhere in the format.

## 7. Version policy

- `ver_major` changes when the meaning of an existing field, the digest
  rule, or the manifest grammar changes. A reader refuses an unknown major.
- `ver_minor` changes for additive changes: new section types, new `flags`
  bits, new `require` names, new BUILD_INFO keys. A reader accepts a higher
  minor and skips unknown section types and vendor sections, but it still
  refuses an unknown `require` name or an unknown `flags` bit, because
  neither can be checked.
- v1 (`version == 1` at offset 4): tools decode it into the same in-memory
  image with each mask bit expanded to an unscoped `require`; the loader
  refuses it with `MXSB_E_VERSION`. `mxsb upgrade` rebuilds it as v2 given a
  manifest and a key, and refuses when a v1 mask bit has no matching line.

## 8. Loader sequence

1. Read 64 bytes. Check magic, major, `header_size`, `file_size`.
2. Read the section table. Check order, alignment, bounds, overlap,
   required and unique types.
3. Compute `digest`; verify SIGNATURE or approvals entry.
4. Parse MANIFEST; check `build_id` and `caps_summary`.
5. Check every `require` against the container manifest (section 9).
6. Mint one capability per `require` line into the new CSpace
   (`CapTable::create_root`).
7. Map TEXT R+X and DATA R+W, zero BSS, jump to `entry_off`.

No byte is mapped before step 6. Every refusal is logged with the step,
section and line, escaped as the Phase 0 supervisor escapes audit text.

## 9. Mapping to the container manifest (`warden.toml`)

| MXSB `require` | `warden.toml` field | admitted when |
|---|---|---|
| `fs.read=P` | `[grants] fs.read = [...]` | P is at or below a listed prefix |
| `fs.write=P` | `fs.write = [...]` | same |
| `exec=P` | `exec = [...]` | same |
| `net.connect=H:P` | `net.connect = [...]` | host and port match a listed entry (`*` only on the container side) |
| `net.listen=H:P` | `net.listen = [...]` | exact match |
| `proc.spawn` | `process.spawn = { max = N }` | N > 0 |
| `render` | `render = true` (new) | present |
| `superop=S` | `superops = [...]` (new) | S listed |
| `clock=C` | `clock = "..."` | container at least as precise |
| `random` | `random = true` (new, default false) | present; nothing is granted by default |
| `memory.max=M` | `memory = { max = ... }` | M <= container max |
| `cpu.max_cores=N` | `cpu = { max_cores = ... }` | N <= container max |

Three fields are added to `warden.toml`: `render`, `superops`, `random`. All default to absent, which is a refusal: Warden has no ambient authority. The kernel thread accepted the four new names as warden-abi 0.2 (additive) and the grant parser enforces their scope rules.
`process.spawn.allow` hashes are MXSB `build_id` values.

## 9a. Exact validation rules (security review finding 7)

These rules bind both the codegen emitter and the loader. A loader applies
them in this order (V27 and V28 run after V13, before the digest) and stops at the first failure with the named error. An
emitter must never produce a file that fails any of them. All arithmetic is
on u64 with explicit overflow checks; a check that would overflow fails.

Input: a byte buffer `buf` of length `len` (the whole file, already read or
mapped read-only; the loader never trusts a length field to read more).

| # | check | error |
|---|---|---|
| V1 | `len >= 64` | `MXSB_E_HEADER` |
| V2 | `buf[0..4] == "MXSB"` | `MXSB_E_MAGIC` |
| V3 | `ver_major == 2` (a `1` here is v1: tools decode, loader refuses) | `MXSB_E_VERSION` |
| V4 | `64 <= header_size <= 4096` and `header_size <= len` | `MXSB_E_HEADER` |
| V5 | `file_size == len` | `MXSB_E_HEADER` |
| V6 | `flags & 0xFFFFFFF8 == 0`; `reserved[16]` all zero | `MXSB_E_HEADER` |
| V7 | `1 <= section_count <= 64` | `MXSB_E_TABLE` |
| V8 | `section_table_off % 16 == 0` and `section_table_off >= header_size` | `MXSB_E_TABLE` |
| V9 | `table_bytes = section_count * 32` (no overflow possible, max 2048); `section_table_off + table_bytes <= len` with checked add | `MXSB_E_TABLE` |
| V10 | for each entry: `reserved == 0`; `flags` has only R/W/X bits and only on TEXT/DATA/BSS; TEXT flags == R+X, DATA flags == R+W, BSS flags == R+W | `MXSB_E_TABLE` |
| V11 | for each non-BSS entry: `offset % 16 == 0`, `offset >= section_table_off + table_bytes`, `size >= 1`, and `offset + size <= len` using checked add (`offset <= len - size`) | `MXSB_E_BOUNDS` |
| V12 | BSS: `offset == 0`; `size <= 1 GiB` (2^30) | `MXSB_E_BOUNDS` |
| V13 | entries sorted by `offset` strictly increasing (excluding BSS); for consecutive entries `prev.offset + prev.size <= next.offset` | `MXSB_E_OVERLAP` |
| V14 | each of types 1..7 appears at most once | `MXSB_E_DUP_SECTION` |
| V15 | MANIFEST, TEXT and SIGNATURE present | `MXSB_E_MISSING_SECTION` |
| V16 | `SIGNATURE.size == 96`; `SUPEROPS.size % 4 == 0`; `MANIFEST.size <= 64 KiB`; `BUILD_INFO.size <= 64 KiB` | `MXSB_E_TABLE` |
| V17 | `TEXT.offset <= entry_off < TEXT.offset + TEXT.size` (the right side cannot overflow after V11) | `MXSB_E_ENTRY` |
| V18 | total mapped size `TEXT.size + DATA.size + BSS.size <= 4 GiB` with checked adds; this is the only allocation the loader makes and it is sized from these checked values, never from `file_size` or a sum of unchecked fields | `MXSB_E_BOUNDS` |
| V19 | `digest == SHA-256(file minus SIGNATURE bytes)` per section 6; the SIGNATURE range is already proven in-bounds by V11 | `MXSB_E_DIGEST` |
| V20 | SIGNATURE valid under a trusted key id, or digest in approvals with `allow_unsigned` | `MXSB_E_SIGNATURE` / `MXSB_E_UNTRUSTED_KEY` |
| V21 | MANIFEST is valid UTF-8, no NUL, no control characters other than `\n` and `\t`, at most 4096 lines, each at most 1024 bytes | `MXSB_E_MANIFEST_SYNTAX` |
| V22 | manifest grammar per section 4, including `mxsb-manifest 1` | `MXSB_E_MANIFEST_SYNTAX` / `MXSB_E_MANIFEST_VERSION` / `MXSB_E_UNKNOWN_PERM` / `MXSB_E_BAD_SCOPE` |
| V23 | `build_id == SHA-256(TEXT bytes || DATA bytes)` | `MXSB_E_BUILD_ID` |
| V24 | `caps_summary == OR(family bits of require lines)`; reserved bits 10..63 zero | `MXSB_E_CAPS_SUMMARY` |
| V25 | every SUPEROPS id has a matching `require superop=<name>` line (name table from the GPU command-set contract) | `MXSB_E_NOT_GRANTED` |
| V26 | every `require` line is an attenuation of the container manifest (section 9); the loader never grants from `caps_summary`, from SUPEROPS, or anything the container does not hold | `MXSB_E_NOT_GRANTED` |
| V27 | every byte not inside the header, the section table or a section is zero (padding between 16-aligned regions and bytes 64..header_size) | `MXSB_E_PADDING` |
| V28 | if DATA or BSS exists, `TEXT.size % 4096 == 0` (section 3.4) | `MXSB_E_LAYOUT` |

Allocation rule: the loader allocates exactly once, after V18, for
`TEXT.size + DATA.size + BSS.size` rounded up to the page size with a
checked add. It copies TEXT and DATA from the already validated ranges and
zero-fills BSS. No `calloc(header + text_size + data_size)` on unchecked
fields, which is the v1 emitter's pattern the review flagged.

Decoder rule: every read of `buf` is through a bounds-checked accessor that
takes `(buf, len, offset, width)` and fails closed. A failed check leaves
the output image zeroed and holds no allocation.

Emitter rule: the codegen emitter runs the same V1..V25, V27 and V28 checks on its own
output before writing the file, with the Rust and C implementations sharing
the golden test set so they agree byte for byte.

## 10. Error codes

`MXSB_E_MAGIC`, `MXSB_E_VERSION`, `MXSB_E_HEADER`, `MXSB_E_TABLE`,
`MXSB_E_BOUNDS`, `MXSB_E_OVERLAP`, `MXSB_E_MISSING_SECTION`,
`MXSB_E_DUP_SECTION`, `MXSB_E_ENTRY`, `MXSB_E_DIGEST`, `MXSB_E_SIGNATURE`,
`MXSB_E_UNTRUSTED_KEY`, `MXSB_E_MANIFEST_SYNTAX`, `MXSB_E_MANIFEST_VERSION`,
`MXSB_E_UNKNOWN_PERM`, `MXSB_E_BAD_SCOPE`, `MXSB_E_BUILD_ID`,
`MXSB_E_CAPS_SUMMARY`, `MXSB_E_NOT_GRANTED`, `MXSB_E_PADDING`, `MXSB_E_LAYOUT`. Numeric values are in the
schema file. A decoder returns exactly one of these or success.

## 11. Changelog

- 2.0.3 (2026-11-05): codegen thread review. v1 header size corrected to 40 bytes. Section 3.4 fixes the load layout (TEXT at base, DATA at base + TEXT.size, BSS after; no vaddr fields). Digest now covers the whole file except SIGNATURE and padding must be zero (V27); V28 requires page-multiple TEXT when DATA or BSS exists. Pre-freeze note: no golden files are frozen yet, so these meaning changes stay under major 2; from the first frozen golden file on, the policy in section 7 applies strictly.
- 2.0.2 (2026-11-05): `random` defaults to false in `warden.toml` (kernel thread amendment); new names recorded as warden-abi 0.2.
- 2.0.1 (2026-11-05): section 9a, exact validation rules V1..V26 and size limits, from security review finding 7.
- 2.0.0 (2026-11-05): first draft of v2. Replaces the 40-byte v1 header.
