# Warden Linux network compartment (first increment)

Linux guests (badges 4–6) are assigned `NETNS_LINUX`; native clients
(badges 1–3) are assigned `NETNS_WARDEN`. The personality server refuses
cross-namespace loopback lookups and allows port reuse in separate namespaces.
This preserves each existing HTTP server/client pair without allowing one
pair to reach the other's listener merely by knowing its port.

**Trust boundary:** badges come from kernel-granted endpoint capabilities.
The namespace mapping lives in the server's compiled, fixed manifest and is
not supplied by guest memory. This is **user-space server enforcement**, not
a new in-kernel namespace implementation. Both namespaces still share a single
server process and fixed-size global socket pools. A flaw in that server can
still affect both; resource exhaustion across pools is not prevented.

This PR is intentionally *not* full Linux userspace compatibility, process
namespaces, a container image loader, mount namespaces, or a network stack.
Those require independent engineering and security review.

Regression:
```sh
cc -std=c11 -Wall -Wextra -Werror tests/compat/net_namespace.c -o /tmp/warden-netns-test
/tmp/warden-netns-test
```
The check covers the shared predicate. A QEMU integration test that proves
native and Linux listeners coexist on an identical port is still needed.
