/* Reproducer for issue #40. Intentionally fails against PR #24 until fixed.
 * cc -std=c11 -O0 -fno-builtin -ffunction-sections -fdata-sections \
 *    -Wl,--gc-sections -Wno-unused-function \
 *    tests/compat/net_namespace_double_bind.c -o /tmp/warden-double-bind
 * /tmp/warden-double-bind
 *
 * This is a native host translation-unit test of personality.c internals,
 * NOT a Warden QEMU test or an isolation/security certification.
 */
#define main personality_original_main
#include "../../crates/warden-kernel/user/personality.c"
#undef main
#include <assert.h>
#include <stdio.h>

static uint8_t buf[BUF_SIZE];

static void set_port(unsigned short port) {
    struct sockaddr_in sa = {0};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    memcpy(buf + BUF_DATA_OFF, &sa, sizeof sa);
}
int main(void) {
    struct client *c = &clients[1];
    c->perms = manifest[1].perms;
    c->netns = manifest[1].netns;
    c->buf = buf;
    int a = (int)do_socket(c, AF_INET, SOCK_STREAM);
    int b = (int)do_socket(c, AF_INET, SOCK_STREAM);
    assert(a >= 0 && b >= 0);
    set_port(8088);
    assert(do_bind(c, a, sizeof(struct sockaddr_in)) == 0);

    /* Binding twice before listen is either refused here or at listen. */
    long b_bind = do_bind(c, b, sizeof(struct sockaddr_in));
    assert(do_listen(c, a) == 0);
    if (b_bind == 0) {
        long second_listen = do_listen(c, b);
        if (second_listen == 0) {
            fprintf(stderr,
                "FAIL #40: same namespace has two active listeners on port 8088\\n");
            return 1;
        }
        assert(second_listen == -EADDRINUSE);
    } else {
        assert(b_bind == -EADDRINUSE);
    }
    /* A separate Linux compartment still owns its own port namespace. */
    struct client *linux = &clients[4];
    linux->perms = manifest[4].perms;
    linux->netns = manifest[4].netns;
    static uint8_t linux_buf[BUF_SIZE];
    linux->buf = linux_buf;
    struct sockaddr_in sa = {0};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(8088);
    memcpy(linux_buf + BUF_DATA_OFF, &sa, sizeof sa);
    int other = (int)do_socket(linux, AF_INET, SOCK_STREAM);
    assert(other >= 0);
    assert(do_bind(linux, other, sizeof(struct sockaddr_in)) == 0);
    assert(do_listen(linux, other) == 0);
    puts("PASS: no duplicate same-namespace listeners; cross-namespace reuse holds");
    return 0;
}
