"""Bounded incremental frame assembler for a byte stream.

Only use after authenticating the transport peer. Never use it as a security
principal or as proof of Warden guest execution.
"""
from frame import MAX_FRAME, FrameError, decode


class FrameReader:
    def __init__(self):
        self._buf = bytearray()
        self._expected = None

    def feed(self, data):
        if not isinstance(data, bytes):
            raise FrameError("feed expects bytes")
        completed = []
        offset = 0
        while offset < len(data):
            if self._expected is None:
                need = 4 - len(self._buf)
                take = min(need, len(data) - offset)
                self._buf.extend(data[offset:offset + take])
                offset += take
                if len(self._buf) != 4:
                    break
                size = int.from_bytes(self._buf, "big")
                if size > MAX_FRAME:
                    self.reset()
                    raise FrameError("oversized frame")
                self._expected = 4 + size
            need = self._expected - len(self._buf)
            take = min(need, len(data) - offset)
            self._buf.extend(data[offset:offset + take])
            offset += take
            if len(self._buf) == self._expected:
                frame = bytes(self._buf)
                self.reset()
                completed.append(decode(frame))
        return completed

    def reset(self):
        self._buf.clear()
        self._expected = None
