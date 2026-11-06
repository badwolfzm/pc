# Warden swarm wire-envelope experiment

This directory is a **host-side Python protocol prototype**, not a running
Warden swarm, guest networking, authenticated discovery, or GPU implementation.
It does not modify the kernel or the existing Network Fabric owner's files.

## Format

Each frame is a four-byte unsigned big-endian JSON byte count, followed by a
canonical ASCII-encoded JSON object. The JSON has exactly five fields:
`version` (integer 1), `kind` (one of hello, offer, accept, result, cancel,
goodbye), `node` and `job` (1–64 ASCII letters/digits/hyphen/underscore),
and `payload` (UTF-8 string, at most 49,152 bytes). The JSON body is at most
65,536 bytes. Keys are sorted, JSON uses compact separators, and Unicode
escapes are canonicalized by the encoder. The parser rejects noncanonical
representations, duplicate keys, unsupported versions, and oversize frames.

`stream.FrameReader` incrementally assembles frames from arbitrarily split or
coalesced byte chunks without buffering more than one bounded frame. A
protocol error is fatal to the connection; the caller must discard its reader
and close the peer transport. Already returned frames are not independently
authenticated.

## Security and compatibility limitations

- **Not authenticated or encrypted.** Before parsing any frame, the real
  transport must authenticate the remote node and bind its authenticated
  identity to the `node` field. This prototype does not do that.
- **Not a job execution engine.** Message names do not implement task
  scheduling, authorization, cancellation, quotas, replay defense, or results.
- **Not an agreed network standard.** Coordinate schema and transport
  integration with the owner of Network Fabric issue #252 before merging.
- **No Warden guest proof.** Python tests on the host are not Warden/QEMU
  execution. A one-vCPU, 512-MiB two-guest acceptance run is still required.
- **Untrusted payloads.** A consuming service must apply separate limits and
  validation to payload content and must not interpret payloads as commands.
- **Reader errors.** Callers must close the connection on parse failure.
  There is no attempt to resynchronize on attacker-controlled bytes.

## Test

From this directory:

```sh
python3 -m unittest -v test_frame.py test_stream.py
```

These tests are proposed; their execution is not claimed by this document.
