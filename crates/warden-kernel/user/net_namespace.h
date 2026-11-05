#ifndef WARDEN_NET_NAMESPACE_H
#define WARDEN_NET_NAMESPACE_H

/* Network compartments of the user-space personality server.
 * A client never chooses its own namespace: the kernel issues its badge,
 * and the fixed server manifest maps that badge to a namespace.
 * Native and Linux guests may use identical ports without sharing sockets.
 * This is a network isolation boundary, not a complete Linux container.
 */
enum { NETNS_NONE = 0, NETNS_WARDEN = 1, NETNS_LINUX = 2 };

static inline int netns_same(unsigned a, unsigned b) {
    return a != NETNS_NONE && a == b;
}

#endif
