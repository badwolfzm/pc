# Warden Network Fabric — connect one-core Warden nodes

**Short-term design:** Keep each Warden guest single-core indefinitely and scale by joining many independently running Warden virtual machines across one or more computers. Every guest is launched with -smp 1. Each authorized physical host runs a small host-side worker; a trusted controller allocates approved jobs across the pool. Native Warden SMP is **not required or planned** for this design.

**Current functional boundary:** the first approved job is booting a SHA-256-pinned Warden multiboot image and verifying the in-guest 201-check suite. There is no general-purpose arbitrary application execution, automatic shared-memory distributed process, or in-guest Warden-to-Warden transport yet. Host-side coordination is a usable basis for adding reviewed workload types.

## Security and limits

- Mutual TLS: the controller validates each worker certificate against a private CA and server hostname/IP. Workers require a client certificate from the same CA. Plain HTTP is denied.
- Remote requests contain only a job ID and approved kernel image hash. Remote clients cannot supply host commands, arbitrary executable files, QEMU switches, or guest memory settings.
- Worker stages a private snapshot of the operator-approved image and verifies its SHA-256 again before each job. It invokes QEMU with a fixed argv, guest network disabled, finite RAM, one vCPU, an output ceiling and a timeout.
- Each host advertises a finite number of concurrent VM slots. The controller schedules at most this many jobs on each node, even when nodes finish at different rates.
- Job IDs deduplicate while retained in the bounded in-memory cache. Not durable across restart/eviction; exactly-once is NOT guaranteed. Network uncertainty fails a job rather than silently duplicating it on another node.
- Keep nodes on a trusted LAN or VPN with firewall restrictions. Do not expose a fabric worker directly on the public Internet. The local QEMU executable is trusted operator configuration and is not separately cryptographically pinned.
- Guest memory allocation of 512 MiB is not the same as 512 MiB host RSS. Each additional VM consumes host resources; monitor host capacity and avoid overcommit.

## Requirements

Python 3.10+, QEMU x86-64, an existing Warden kernel multiboot32 image, and OpenSSL for issuing private certificates.

## Easy onboarding for a private network

On one trusted administrator computer, generate the private CA and per-node identities (replace these example IPs with your two real LAN/VPN node addresses):

~~~bash
bash tools/warden-cpu-fabric/bootstrap-certs.sh ~/warden-fabric-secret 192.168.10.21 192.168.10.22
~~~

The resulting folder contains ca.pem, ca.key, controller.pem, controller.key, node-1.pem/key and node-2.pem/key. Protect the entire folder and **never copy ca.key to workers**. Transfer each worker only its own node certificate/key and ca.pem using an authenticated secure channel. Keep controller.key only on the controller. Worker certificates must include the actual IP or DNS name used by the controller.

On *each worker*, copy the **same reviewed** Warden image or build it from a pinned commit; compute its SHA:

~~~bash
sha256sum /path/to/warden-kernel.mb32
~~~

Start **worker 1** on Linux, with its own node-1 identity:

~~~bash
python3 tools/warden-cpu-fabric/fabric.py worker \
  --qemu /usr/bin/qemu-system-x86_64 \
  --image /path/to/warden-kernel.mb32 \
  --image-sha256 YOUR_64_CHARACTER_SHA256 \
  --accel tcg --memory-mib 512 --slots 2 \
  --listen 0.0.0.0 --port 9443 \
  --tls-ca /secure/ca.pem \
  --tls-cert /secure/node-1.pem --tls-key /secure/node-1.key
~~~

On **worker 2**, use its node-2 cert/key and the same image SHA. Slots may differ based on each host's capacity.

On the **controller**, create nodes.json:

~~~json
{"nodes":["https://192.168.10.21:9443","https://192.168.10.22:9443"]}
~~~

Run independent Warden jobs across all available hosts:

~~~bash
python3 tools/warden-cpu-fabric/fabric.py run \
  --nodes nodes.json --image-sha256 YOUR_64_CHARACTER_SHA256 \
  --jobs 4 --parallel 4 \
  --tls-ca /secure/ca.pem \
  --tls-cert /secure/controller.pem --tls-key /secure/controller.key
~~~

Each job returns a JSON verdict and node address. Successful overall output ends with FABRIC SUMMARY: 4/4 passed.

**To add another computer:** sign one more unique worker certificate with a SAN for its IP/DNS, install the reviewed Warden image and worker with its own finite slot budget, add its HTTPS address to nodes.json, and rerun the coordinator. No changes to existing one-core Warden kernels are required.

## Your Windows QEMU WHPX machine

Run the worker using native Windows Python from CMD. First securely provision the Windows certificate directory with the worker's own identity:

~~~cmd
python tools\warden-cpu-fabric\fabric.py worker --qemu "C:\Program Files\qemu\qemu-system-x86_64.exe" --image "%USERPROFILE%\Downloads\warden-whpx-fix.mb32" --image-sha256 YOUR_64_CHARACTER_SHA256 --accel whpx --memory-mib 512 --slots 1 --listen 127.0.0.1 --port 9443 --tls-ca "C:\warden-certs\ca.pem" --tls-cert "C:\warden-certs\node-1.pem" --tls-key "C:\warden-certs\node-1.key"
~~~

For access from other machines, listen on a firewall-restricted LAN/VPN interface, with a certificate matching its actual name/IP. WSL2 NAT and Windows Firewall rules may require configuration. Never open a node to an untrusted network.

## Real tests

~~~bash
python3 -m unittest discover -s tools/warden-cpu-fabric -p 'test_*.py' -v
bash tools/warden-cpu-fabric/two-node-ci.sh /absolute/path/to/warden-kernel.mb32
~~~

The integration script issues test-only certificates, runs two independent authenticated host-side worker endpoints and requires four real Warden/QEMU kernel-suite jobs to pass. This does not claim two *physical* hosts have been tested.

## Future generalization without SMP

Expand to a secure operator-approved job catalog and signed input/result artifacts, then add a capability-bound in-guest task ingress and egress bridge. This is separate from a host-controlled QEMU test fleet: Warden guests currently use -nic none and are not themselves network peers. Make retries and persistent job journals explicit, avoid granting arbitrary shell/file access, and measure real per-node throughput. An unmodified single-threaded program cannot automatically borrow remote cores without redesigning that program as distributed tasks.

Tracked by GitHub issue #252. Do not claim unlimited physical CPU or verified native in-guest networking.
