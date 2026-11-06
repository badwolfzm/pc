"""Strict, bounded wire envelope parser for Warden Network Fabric prototypes.

This is NOT authentication, encryption, or a guest network transport.
A separate authenticated transport must establish peer identity before use.
"""
import json

MAX_FRAME = 65536
MAX_PAYLOAD = 49152
KINDS = frozenset(("hello", "offer", "accept", "result", "cancel", "goodbye"))
FIELDS = frozenset(("version", "kind", "node", "job", "payload"))


class FrameError(ValueError):
    pass


def encode(kind, node, job, payload):
    obj = {"version": 1, "kind": kind, "node": node, "job": job, "payload": payload}
    _validate(obj)
    data = json.dumps(obj, ensure_ascii=True, sort_keys=True,
                      separators=(",", ":"), allow_nan=False).encode("ascii")
    if len(data) > MAX_FRAME:
        raise FrameError("frame exceeds limit")
    return len(data).to_bytes(4, "big") + data


def decode(frame):
    if not isinstance(frame, bytes) or len(frame) < 4:
        raise FrameError("missing length prefix")
    size = int.from_bytes(frame[:4], "big")
    if size > MAX_FRAME or size != len(frame) - 4:
        raise FrameError("invalid length")
    try:
        raw = frame[4:].decode("utf-8", errors="strict")
        def unique(pairs):
            out = {}
            for key, value in pairs:
                if key in out:
                    raise FrameError("duplicate key")
                out[key] = value
            return out
        obj = json.loads(raw, object_pairs_hook=unique,
                         parse_constant=lambda _: (_ for _ in ()).throw(FrameError("nonfinite")))
    except (UnicodeError, ValueError, TypeError) as exc:
        raise FrameError("invalid JSON") from exc
    _validate(obj)
    if encode(obj["kind"], obj["node"], obj["job"], obj["payload"])[4:] != frame[4:]:
        raise FrameError("noncanonical encoding")
    return obj


def _validate(obj):
    if not isinstance(obj, dict) or set(obj) != FIELDS:
        raise FrameError("unknown or missing fields")
    if type(obj["version"]) is not int or obj["version"] != 1:
        raise FrameError("unsupported version")
    if obj["kind"] not in KINDS:
        raise FrameError("unsupported kind")
    for field in ("node", "job"):
        value = obj[field]
        if not isinstance(value, str) or not (1 <= len(value) <= 64):
            raise FrameError("invalid identifier")
        if not all(c.isascii() and (c.isalnum() or c in "-_") for c in value):
            raise FrameError("invalid identifier")
    payload = obj["payload"]
    if not isinstance(payload, str) or len(payload.encode("utf-8")) > MAX_PAYLOAD:
        raise FrameError("invalid payload")
