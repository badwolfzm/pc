"""Host-only bounded, immutable artifact store reference for authorized directories.

Not a production Warden driver or substitute for a kernel capability check.
A caller must supply an already approved dedicated root; do not mount devices here.
"""
import hashlib
import json
import os
import re
import secrets
import stat

HEX = re.compile(r"^[0-9a-f]{64}$")
NAME = re.compile(r"^[a-zA-Z0-9][a-zA-Z0-9_-]{0,63}$")
MAX_OBJECT = 8 * 1024 * 1024
MAX_NAMESPACE = 64 * 1024 * 1024
O_DIR = os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC
O_FILE = os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC

class StoreError(Exception):
    pass

class Store:
    def __init__(self, authorized_root, namespace, max_object=MAX_OBJECT, quota=MAX_NAMESPACE):
        if not NAME.fullmatch(namespace):
            raise StoreError("invalid namespace")
        if not (0 < max_object <= MAX_OBJECT and max_object <= quota <= MAX_NAMESPACE):
            raise StoreError("invalid quota")
        self.max_object, self.quota = max_object, quota
        self.root_fd = None
        self.ns_fd = None
        try:
            self.root_fd = os.open(authorized_root, O_DIR)
            os.mkdir(namespace, 0o700, dir_fd=self.root_fd) if not self._exists(namespace) else None
            self.ns_fd = os.open(namespace, O_DIR, dir_fd=self.root_fd)
            self._ensure_dir("objects")
        except BaseException:
            self.close()
            raise

    def _exists(self, name):
        try:
            os.stat(name, dir_fd=self.root_fd, follow_symlinks=False)
            return True
        except FileNotFoundError:
            return False

    def _ensure_dir(self, name):
        try:
            os.mkdir(name, 0o700, dir_fd=self.ns_fd)
        except FileExistsError:
            pass
        fd = os.open(name, O_DIR, dir_fd=self.ns_fd)
        os.close(fd)

    def close(self):
        for attr in ("ns_fd", "root_fd"):
            fd = getattr(self, attr, None)
            if fd is not None:
                os.close(fd)
                setattr(self, attr, None)

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()

    def _objects(self):
        if self.ns_fd is None:
            raise StoreError("closed")
        return os.open("objects", O_DIR, dir_fd=self.ns_fd)

    def _used(self, directory):
        total = 0
        for name in os.listdir(directory):
            if not HEX.fullmatch(name):
                raise StoreError("unexpected object name")
            info = os.stat(name, dir_fd=directory, follow_symlinks=False)
            if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1:
                raise StoreError("unsafe object")
            total += info.st_size
            if total > self.quota:
                raise StoreError("quota already exceeded")
        return total

    def _get(self, directory, digest):
        fd = os.open(digest, O_FILE | os.O_NONBLOCK, dir_fd=directory)
        try:
            info = os.fstat(fd)
            if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1 or info.st_size > self.max_object:
                raise StoreError("unsafe or oversized object")
            # Strict cap and repeated-size check; never return unauthenticated data.
            data = bytearray()
            while len(data) <= self.max_object:
                block = os.read(fd, min(65536, self.max_object + 1 - len(data)))
                if not block:
                    break
                data.extend(block)
            if len(data) != info.st_size or hashlib.sha256(data).hexdigest() != digest:
                raise StoreError("corrupt or changed object")
            return bytes(data)
        finally:
            os.close(fd)

    def get(self, digest):
        if not isinstance(digest, str) or not HEX.fullmatch(digest):
            raise StoreError("invalid digest")
        fd = self._objects()
        try:
            return self._get(fd, digest)
        finally:
            os.close(fd)

    def put(self, data):
        if not isinstance(data, bytes) or len(data) > self.max_object:
            raise StoreError("object must be bounded bytes")
        digest = hashlib.sha256(data).hexdigest()
        directory = self._objects()
        temp = ".tmp-" + secrets.token_hex(16)
        try:
            try:
                prior = self._get(directory, digest)
            except FileNotFoundError:
                prior = None
            if prior is not None:
                if prior != data:
                    raise StoreError("digest collision")
                return digest
            if self._used(directory) + len(data) > self.quota:
                raise StoreError("namespace quota exceeded")
            flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW | os.O_CLOEXEC
            out = os.open(temp, flags, 0o600, dir_fd=directory)
            try:
                with os.fdopen(out, "wb", closefd=False) as stream:
                    stream.write(data)
                    stream.flush()
                    os.fsync(out)
            finally:
                os.close(out)
            try:
                os.link(temp, digest, src_dir_fd=directory, dst_dir_fd=directory, follow_symlinks=False)
            except FileExistsError:
                if self._get(directory, digest) != data:
                    raise StoreError("concurrent object mismatch")
            os.fsync(directory)
            return digest
        finally:
            try:
                os.unlink(temp, dir_fd=directory)
            except FileNotFoundError:
                pass
            os.close(directory)

    def put_manifest(self, kind, inputs, payload_digest, build_id):
        if not NAME.fullmatch(kind) or not NAME.fullmatch(build_id):
            raise StoreError("invalid kind or build id")
        if not isinstance(inputs, dict) or len(inputs) > 128:
            raise StoreError("invalid input map")
        for name, digest in inputs.items():
            if not isinstance(name, str) or not NAME.fullmatch(name) or not isinstance(digest, str) or not HEX.fullmatch(digest):
                raise StoreError("invalid input reference")
        if not isinstance(payload_digest, str) or not HEX.fullmatch(payload_digest):
            raise StoreError("invalid payload hash")
        # Local presence is verified at publication; future gets still check integrity.
        self.get(payload_digest)
        manifest = dict(schema=1, kind=kind, build_id=build_id,
                        inputs=inputs, payload=payload_digest)
        return self.put(json.dumps(manifest, sort_keys=True, separators=(",", ":")).encode("utf-8"))
