"""Fail-closed reference for a Warden Linux syscall mirror, NOT guest runtime."""
from dataclasses import dataclass
from enum import Enum
from typing import Callable, Mapping

MAX_COPY = 16384 - 64
MAX_PATH = 255

class Route(Enum):
    LOCAL = "kernel-local"
    MEDIATED = "personality"
    DENIED = "denied"
    UNSUPPORTED = "unsupported"

# x86-64 UAPI syscall numbers. These are proposed mirror routes, not claims
# that every LOCAL syscall is implemented by Warden today.
LOCAL = frozenset({9, 10, 11, 12, 24, 39, 60, 63, 158, 186, 202, 218, 228, 231})
MEDIATED = frozenset({0, 1, 2, 3, 41, 42, 43, 49, 50, 232, 233, 257, 288, 291})
DENIED = frozenset({56, 57, 58, 59, 101, 165, 169, 246, 298, 321})

@dataclass(frozen=True)
class Grant:
    badge: int
    fs_read: bool = False
    net_connect: bool = False
    net_listen: bool = False

@dataclass(frozen=True)
class Request:
    number: int
    args: tuple[int, int, int, int, int, int]
    payload: bytes = b""  # trusted immutable copy; NEVER a live guest pointer

@dataclass(frozen=True)
class Decision:
    route: Route
    errno: int = 0
    reason: str = ""

def decide(grant: Grant | None, req: Request) -> Decision:
    if grant is None or not isinstance(grant.badge, int) or not 1 <= grant.badge <= 65535:
        return Decision(Route.DENIED, 1, "unauthenticated kernel badge")
    if not isinstance(req.number, int) or len(req.args) != 6 or any(not isinstance(x, int) for x in req.args):
        return Decision(Route.DENIED, 22, "invalid arguments")
    if not isinstance(req.payload, bytes) or len(req.payload) > MAX_COPY:
        return Decision(Route.DENIED, 14, "unsafe buffer")
    n = req.number
    if n in DENIED:
        return Decision(Route.DENIED, 1, "escape operation")
    if n in LOCAL:
        return Decision(Route.LOCAL)
    if n not in MEDIATED:
        return Decision(Route.UNSUPPORTED, 38, "unimplemented")
    if n in (2, 257):
        if not grant.fs_read:
            return Decision(Route.DENIED, 13, "fs.read not granted")
        if req.args[1] != 0 or not 1 <= req.args[2] <= MAX_PATH:
            return Decision(Route.DENIED, 13, "read-only path required")
        if len(req.payload) != req.args[2] or b"\x00" in req.payload:
            return Decision(Route.DENIED, 22, "invalid path snapshot")
    if n == 41 and not (grant.net_connect or grant.net_listen):
        return Decision(Route.DENIED, 13, "network not granted")
    if n == 42 and not grant.net_connect:
        return Decision(Route.DENIED, 13, "net.connect not granted")
    if n in (49, 50) and not grant.net_listen:
        return Decision(Route.DENIED, 13, "net.listen not granted")
    if n in (0, 1) and not 0 <= req.args[2] <= MAX_COPY:
        return Decision(Route.DENIED, 14, "invalid transfer length")
    if n == 1 and len(req.payload) != req.args[2]:
        return Decision(Route.DENIED, 14, "write copy does not match count")
    return Decision(Route.MEDIATED)

def dispatch(grants: Mapping[int, Grant], badge: int, req: Request,
             service: Callable[[int, Request], int]) -> int:
    """Badge and grants MUST originate at trusted kernel boundary."""
    grant = grants.get(badge)
    if grant is None or grant.badge != badge:
        return -1
    decision = decide(grant, req)
    if decision.route is Route.MEDIATED:
        return service(badge, req)
    if decision.route is Route.LOCAL:
        return -38  # local operation must be implemented by the real kernel
    return -decision.errno
