#!/usr/bin/env python3
"""Warden CPU Fabric: authenticated, bounded, multi-host one-vCPU QEMU workers.

This is a *host-side fleet*, not SMP inside the Warden guest. It deliberately
offers only the fixed Warden kernel-test job, never arbitrary guest code or
commands from a network request.
"""
from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import http.server
import json
import os
from pathlib import Path
import re
import shutil
import socket
import ssl
import subprocess
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
import uuid

MAX_BODY = 1024
MAX_RESPONSE = 4096
MAX_LOG = 1024 * 1024
MAX_CACHE = 256
JOB_ID = re.compile(r"[A-Za-z0-9_-]{1,80}\Z")
SHA256 = re.compile(r"[a-f0-9]{64}\Z")
VERDICT = re.compile(r"^ALL HELD: ([0-9]+) checks, 0 failed", re.MULTILINE)


class PolicyError(ValueError):
    pass


class BusyError(Exception):
    pass


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(65536), b""):
            h.update(block)
    return h.hexdigest()


def validate_sha(value: str) -> str:
    if not isinstance(value, str) or SHA256.fullmatch(value) is None:
        raise PolicyError("expected a lowercase 64-character SHA-256 digest")
    return value


def validate_url(url: str) -> str:
    if not isinstance(url, str):
        raise PolicyError("node URL must be a string")
    parsed = urllib.parse.urlsplit(url)
    if (parsed.scheme != "https" or not parsed.hostname or parsed.username
            or parsed.password or parsed.path not in ("", "/")
            or parsed.query or parsed.fragment or parsed.port is None):
        raise PolicyError("nodes must use https://hostname:port with no path or credentials")
    return url.rstrip("/")


class Policy:
    def __init__(self, qemu: str, image: str, image_sha256: str,
                 accel: str = "tcg", memory_mib: int = 512,
                 checks: int = 201, timeout: float = 120.0,
                 log_cap: int = MAX_LOG):
        self.qemu = str(Path(qemu).resolve(strict=True))
        self.image = Path(image).resolve(strict=True)
        self.image_sha256 = validate_sha(image_sha256)
        self.accel = accel
        self.memory_mib = memory_mib
        self.checks = checks
        self.timeout = timeout
        self.log_cap = log_cap
        if accel not in ("tcg", "kvm", "whpx"):
            raise PolicyError("accel must be tcg, kvm, or whpx")
        if not isinstance(memory_mib, int) or not 64 <= memory_mib <= 131072:
            raise PolicyError("memory must be 64..131072 MiB")
        if not isinstance(checks, int) or not 1 <= checks <= 1000000:
            raise PolicyError("expected checks must be positive")
        if not 0.05 <= timeout <= 3600:
            raise PolicyError("timeout must be 0.05..3600 seconds")
        if not 1024 <= log_cap <= MAX_LOG:
            raise PolicyError("log cap must be between 1024 and 1048576")
        if not self.image.is_file() or not Path(self.qemu).is_file():
            raise PolicyError("QEMU and Warden kernel image must be regular files")
        if sha256_file(self.image) != self.image_sha256:
            raise PolicyError("kernel image digest differs from operator-pinned SHA-256")

    def argv(self, staged_image: Path) -> list[str]:
        return [
            self.qemu, "-accel", self.accel, "-machine", "pc", "-smp", "1",
            "-m", str(self.memory_mib) + "M", "-nic", "none",
            "-monitor", "none", "-display", "none", "-serial", "stdio",
            "-no-reboot", "-device",
            "isa-debug-exit,iobase=0xf4,iosize=0x04",
            "-kernel", str(staged_image),
        ]


def run_warden(policy: Policy) -> dict:
    """Stage a private digest-verified image, run real QEMU, enforce bounds."""
    start = time.monotonic()
    with tempfile.TemporaryDirectory(prefix="warden-fabric-") as temp:
        staged = Path(temp) / "warden-kernel.mb32"
        # The guest image is a private snapshot. A concurrent host-side update
        # to the operator's original path cannot change this job after hashing.
        with policy.image.open("rb") as src, staged.open("xb") as dst:
            shutil.copyfileobj(src, dst)
        if sha256_file(staged) != policy.image_sha256:
            return {"ok": False, "error": "kernel-image-changed"}
        staged.chmod(0o400)
        with tempfile.TemporaryFile(mode="w+b") as output:
            try:
                proc = subprocess.Popen(
                    policy.argv(staged), stdin=subprocess.DEVNULL,
                    stdout=output, stderr=subprocess.STDOUT, shell=False,
                    close_fds=True)
            except OSError:
                return {"ok": False, "error": "qemu-launch-failed"}
            reason = None
            while True:
                code = proc.poll()
                if os.fstat(output.fileno()).st_size > policy.log_cap:
                    reason = "output-limit"
                    break
                if code is not None:
                    break
                if time.monotonic() - start > policy.timeout:
                    reason = "timeout"
                    break
                time.sleep(0.02)
            if reason is not None:
                proc.kill()
            code = proc.wait()
            output.seek(0)
            raw = output.read(policy.log_cap + 1)
    elapsed = round((time.monotonic() - start) * 1000)
    if reason is not None:
        return {"ok": False, "error": reason, "elapsed_ms": elapsed,
                "exit_code": code}
    if len(raw) > policy.log_cap:
        return {"ok": False, "error": "output-limit", "elapsed_ms": elapsed,
                "exit_code": code}
    log = raw.decode("utf-8", errors="replace")
    matches = VERDICT.findall(log)
    ok = (code == 1 and len(matches) == 1
          and int(matches[0]) == policy.checks
          and not re.search(r"^FAIL |KERNEL TEST FAILED", log, re.MULTILINE))
    return {"ok": ok, "checks": int(matches[0]) if len(matches) == 1 else None,
            "exit_code": code, "elapsed_ms": elapsed,
            "log_sha256": hashlib.sha256(raw).hexdigest(),
            "error": None if ok else "guest-suite-failed"}


class WorkPool:
    """Bounded running tasks, plus a bounded in-memory idempotency cache.

    A job ID is deduplicated while retained. This is NOT durable exactly-once
    execution: after cache eviction or process restart, a job could rerun.
    """

    def __init__(self, slots: int, image_sha256: str, runner=run_warden,
                 policy: Policy | None = None):
        if not 1 <= slots <= 256:
            raise PolicyError("slots must be between 1 and 256 per node")
        self.slots = slots
        self.image_sha256 = validate_sha(image_sha256)
        self.policy = policy
        self.runner = runner
        self.executor = concurrent.futures.ThreadPoolExecutor(max_workers=slots)
        self.lock = threading.Lock()
        self.active = 0
        self.cache: dict[str, concurrent.futures.Future] = {}

    def submit(self, job_id: str, digest: str):
        if not isinstance(job_id, str) or not JOB_ID.fullmatch(job_id):
            raise PolicyError("invalid job ID")
        if digest != self.image_sha256:
            raise PolicyError("unapproved kernel image hash")
        with self.lock:
            existing = self.cache.get(job_id)
            if existing is not None:
                return existing, False
            if self.active >= self.slots:
                raise BusyError("worker capacity exhausted")
            if len(self.cache) >= MAX_CACHE:
                for key, future in list(self.cache.items()):
                    if future.done():
                        del self.cache[key]
                        break
                else:
                    raise BusyError("worker idempotency cache full")
            self.active += 1
            future = self.executor.submit(self._execute)
            self.cache[job_id] = future
            return future, True

    def _execute(self):
        try:
            return self.runner(self.policy)
        except Exception:
            # A failure is visible but cannot leak host paths or tracebacks
            # over the authenticated network boundary.
            return {"ok": False, "error": "worker-execution-failed"}
        finally:
            with self.lock:
                self.active -= 1

    def status(self, job_id: str):
        if not isinstance(job_id, str) or JOB_ID.fullmatch(job_id) is None:
            raise PolicyError("invalid job ID")
        with self.lock:
            future = self.cache.get(job_id)
        if future is None:
            return None
        if not future.done():
            return {"state": "running", "job_id": job_id}
        return {"state": "finished", "job_id": job_id, "result": future.result()}

    def health(self):
        with self.lock:
            active = self.active
        return {"version": 1, "image_sha256": self.image_sha256,
                "slots": self.slots, "active": active, "vcpus_per_job": 1}

    def close(self):
        self.executor.shutdown(wait=True)


def handler_for(pool: WorkPool):
    class Handler(http.server.BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.0"

        def log_message(self, fmt, *args):
            # Deliberately no user-controlled values in logs.
            pass

        def send_json(self, code: int, data: dict):
            payload = json.dumps(data, separators=(",", ":")).encode("utf-8")
            self.send_response(code)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(payload)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(payload)

        def do_GET(self):
            if self.path == "/v1/health":
                return self.send_json(200, pool.health())
            if self.path.startswith("/v1/jobs/"):
                jid = self.path[len("/v1/jobs/"):]
                try:
                    result = pool.status(jid)
                except PolicyError:
                    return self.send_json(400, {"error": "invalid-job-id"})
                if result is None:
                    return self.send_json(404, {"error": "unknown-job"})
                return self.send_json(200, result)
            return self.send_json(404, {"error": "unknown-route"})

        def do_POST(self):
            if self.path != "/v1/jobs":
                return self.send_json(404, {"error": "unknown-route"})
            try:
                size = int(self.headers.get("Content-Length", "-1"))
                if not 0 < size <= MAX_BODY:
                    raise ValueError()
                raw = self.rfile.read(size)
                if len(raw) != size:
                    raise ValueError()
                item = json.loads(raw)
                if not isinstance(item, dict) or set(item) != {"job_id", "image_sha256"}:
                    raise ValueError()
                _, created = pool.submit(item["job_id"], item["image_sha256"])
                status = pool.status(item["job_id"])
                return self.send_json(202 if created else 200, status)
            except BusyError:
                return self.send_json(429, {"error": "capacity-exhausted"})
            except (ValueError, TypeError, PolicyError, UnicodeError):
                return self.send_json(400, {"error": "invalid-request"})
    return Handler


class BoundedServer(http.server.ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True
    request_queue_size = 16


class RejectRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        raise PolicyError("redirects are forbidden")


def tls_client(ca: str, cert: str, key: str):
    ctx = ssl.create_default_context(ssl.Purpose.SERVER_AUTH, cafile=ca)
    ctx.load_cert_chain(certfile=cert, keyfile=key)
    ctx.check_hostname = True
    ctx.verify_mode = ssl.CERT_REQUIRED
    return urllib.request.build_opener(
        urllib.request.HTTPSHandler(context=ctx), RejectRedirect())


def api(opener, url: str, path: str, data: dict | None = None,
        timeout: float = 10.0):
    blob = json.dumps(data, separators=(",", ":")).encode() if data is not None else None
    request = urllib.request.Request(
        url + path, data=blob,
        headers={"Content-Type": "application/json"} if blob else {},
        method="POST" if blob is not None else "GET")
    with opener.open(request, timeout=timeout) as response:
        content = response.read(MAX_RESPONSE + 1)
        if len(content) > MAX_RESPONSE:
            raise PolicyError("worker response too large")
    parsed = json.loads(content)
    if not isinstance(parsed, dict):
        raise PolicyError("invalid worker JSON")
    return parsed


def read_nodes(path: str) -> list[str]:
    data = json.loads(Path(path).read_text(encoding="utf-8"))
    if not isinstance(data, dict) or set(data) != {"nodes"} or not isinstance(data["nodes"], list):
        raise PolicyError('node config must be {"nodes": ["https://host:port"]}')
    nodes = [validate_url(url) for url in data["nodes"]]
    if not nodes or len(set(nodes)) != len(nodes):
        raise PolicyError("node list must be nonempty with unique URLs")
    return nodes


def coordinate(opener, nodes: list[str], digest: str, jobs: int,
               max_parallel: int = 16, seconds: float = 130.0):
    if not 1 <= jobs <= 1000000:
        raise PolicyError("jobs must be 1..1000000")
    if not 1 <= max_parallel <= 4096:
        raise PolicyError("max parallel must be 1..4096")
    targets = []
    for url in nodes:
        health = api(opener, url, "/v1/health")
        if (health.get("image_sha256") != digest
                or health.get("vcpus_per_job") != 1
                or not isinstance(health.get("slots"), int)
                or not 1 <= health["slots"] <= 256):
            raise PolicyError("worker capacity or kernel identity mismatch")
        targets.extend([url] * health["slots"])
    parallel = min(max_parallel, len(targets), jobs)
    prefix = uuid.uuid4().hex[:16]

    def execute(index):
        url = targets[index % len(targets)]
        job_id = prefix + "-" + str(index)
        try:
            # If the response is lost, never reassign this job to another
            # machine: an already-running guest could execute twice.
            response = api(opener, url, "/v1/jobs",
                           {"job_id": job_id, "image_sha256": digest})
            if response.get("job_id") != job_id:
                raise PolicyError("mismatched accepted job ID")
            deadline = time.monotonic() + seconds
            while True:
                state = response.get("state")
                if state == "finished":
                    result = response.get("result")
                    if not isinstance(result, dict):
                        raise PolicyError("missing result")
                    return {"job_id": job_id, "node": url, **result}
                if state != "running":
                    raise PolicyError("unknown worker state")
                if time.monotonic() >= deadline:
                    return {"job_id": job_id, "node": url, "ok": False,
                            "error": "result-deadline-exceeded"}
                time.sleep(0.05)
                response = api(opener, url, "/v1/jobs/" + job_id, timeout=10.0)
        except (PolicyError, urllib.error.URLError, OSError, ValueError) as exc:
            # Fail closed on uncertain delivery; don't silently retry on
            # other hosts (distributed exactly-once is not guaranteed).
            return {"job_id": job_id, "node": url, "ok": False,
                    "error": "network-or-protocol-failure"}

    good = 0
    done = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=parallel) as executor:
        pending = {}
        next_index = 0
        while next_index < jobs and len(pending) < parallel:
            f = executor.submit(execute, next_index)
            pending[f] = next_index
            next_index += 1
        while pending:
            finished, _ = concurrent.futures.wait(
                pending, return_when=concurrent.futures.FIRST_COMPLETED)
            for future in finished:
                pending.pop(future)
                result = future.result()
                print(json.dumps(result, sort_keys=True), flush=True)
                done += 1
                good += bool(result.get("ok"))
                if next_index < jobs:
                    f = executor.submit(execute, next_index)
                    pending[f] = next_index
                    next_index += 1
    print("FABRIC SUMMARY: " + str(good) + "/" + str(done) + " passed")
    return good == jobs


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="mode", required=True)
    w = sub.add_parser("worker", help="start a mutually authenticated TLS worker")
    w.add_argument("--qemu", required=True)
    w.add_argument("--image", required=True)
    w.add_argument("--image-sha256", required=True)
    w.add_argument("--accel", choices=("tcg", "kvm", "whpx"), default="tcg")
    w.add_argument("--memory-mib", type=int, default=512)
    w.add_argument("--checks", type=int, default=201)
    w.add_argument("--timeout", type=float, default=120.0)
    w.add_argument("--slots", type=int, default=1)
    w.add_argument("--listen", default="127.0.0.1")
    w.add_argument("--port", type=int, default=9443)
    w.add_argument("--tls-cert", required=True)
    w.add_argument("--tls-key", required=True)
    w.add_argument("--tls-ca", required=True)
    c = sub.add_parser("run", help="run a bounded number of jobs on trusted nodes")
    c.add_argument("--nodes", required=True)
    c.add_argument("--image-sha256", required=True)
    c.add_argument("--jobs", type=int, required=True)
    c.add_argument("--parallel", type=int, default=16)
    c.add_argument("--timeout", type=float, default=130.0)
    c.add_argument("--tls-cert", required=True)
    c.add_argument("--tls-key", required=True)
    c.add_argument("--tls-ca", required=True)
    args = parser.parse_args(argv)
    try:
        digest = validate_sha(args.image_sha256)
        if args.mode == "run":
            opener = tls_client(args.tls_ca, args.tls_cert, args.tls_key)
            return 0 if coordinate(opener, read_nodes(args.nodes), digest,
                                   args.jobs, args.parallel, args.timeout) else 1
        policy = Policy(args.qemu, args.image, digest, accel=args.accel,
                        memory_mib=args.memory_mib, checks=args.checks,
                        timeout=args.timeout)
        pool = WorkPool(args.slots, digest, policy=policy)
        server = BoundedServer((args.listen, args.port), handler_for(pool))
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.minimum_version = ssl.TLSVersion.TLSv1_2
        context.verify_mode = ssl.CERT_REQUIRED
        context.load_verify_locations(cafile=args.tls_ca)
        context.load_cert_chain(certfile=args.tls_cert, keyfile=args.tls_key)
        server.socket = context.wrap_socket(server.socket, server_side=True)
        print("Warden CPU Fabric worker listening on " +
              args.listen + ":" + str(server.server_port) +
              " slots=" + str(args.slots), flush=True)
        try:
            server.serve_forever()
        finally:
            server.server_close()
            pool.close()
        return 0
    except (PolicyError, OSError, ssl.SSLError) as exc:
        print("fabric: " + str(exc), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
