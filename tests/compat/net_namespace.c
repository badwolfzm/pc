/* Run from the repository root:
 * cc -std=c11 -Wall -Wextra -Werror tests/compat/net_namespace.c -o /tmp/warden-netns-test
 * /tmp/warden-netns-test
 */
#include "../../crates/warden-kernel/user/net_namespace.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    assert(netns_same(NETNS_WARDEN, NETNS_WARDEN));
    assert(netns_same(NETNS_LINUX, NETNS_LINUX));
    assert(!netns_same(NETNS_WARDEN, NETNS_LINUX));
    assert(!netns_same(NETNS_LINUX, NETNS_WARDEN));
    assert(!netns_same(NETNS_NONE, NETNS_WARDEN));
    assert(!netns_same(NETNS_NONE, NETNS_NONE));
    puts("PASS: network namespace boundary");
    return 0;
}
