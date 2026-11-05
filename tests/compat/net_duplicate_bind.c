/* Host-level integration regression for issue #40.
 * Exercises the real personality socket/stream/listener operations.
 * Build from repo root with:
 * cc -std=c11 -O0 -fno-builtin -ffunction-sections -fdata-sections
 *    -Wl,--gc-sections -Wno-unused-function
 *    tests/compat/net_duplicate_bind.c -o /tmp/warden-duplicate-bind
 * /tmp/warden-duplicate-bind
 */
#define main warden_personality_guest_main
#include "../../crates/warden-kernel/user/personality.c"
#undef main
#include <assert.h>
#include <stdio.h>

static uint8_t buffers[7][BUF_SIZE];

static void init(void) {
    memset(clients, 0, sizeof clients);
    memset(streams, 0, sizeof streams);
    memset(listeners, 0, sizeof listeners);
    for (int i = 1; i <= 6; i++) {
        clients[i].perms = manifest[i].perms;
        clients[i].netns = manifest[i].netns;
        clients[i].buf = buffers[i];
    }
}
static int socket_for(int badge) {
    long fd = do_socket(&clients[badge], AF_INET, SOCK_STREAM);
    assert(fd >= 0);
    return (int)fd;
}
static long bind_port(int badge, int fd, unsigned short port) {
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    memcpy(buffers[badge] + BUF_DATA_OFF, &a, sizeof a);
    return do_bind(&clients[badge], fd, sizeof a);
}
static long connect_port(int badge, int fd, unsigned short port) {
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    memcpy(buffers[badge] + BUF_DATA_OFF, &a, sizeof a);
    return do_connect(&clients[badge], fd, sizeof a);
}
static int num_used_streams(void) {
    int n = 0;
    for (int i = 0; i < MAX_STREAM; i++) n += streams[i].used != 0;
    return n;
}
static int num_used_listeners(void) {
    int n = 0;
    for (int i = 0; i < MAX_LISTEN; i++) n += listeners[i].used != 0;
    return n;
}
int main(void) {
    init();
    int a = socket_for(1), b = socket_for(1);
    assert(bind_port(1, a, 8088) == 0);
    const int streams_after_a = num_used_streams();
    assert(bind_port(1, b, 8088) == -EADDRINUSE);
    assert(num_used_streams() == streams_after_a);
    assert(num_used_listeners() == 0);
    assert(do_listen(&clients[1], b) == -EINVAL);
    assert(do_listen(&clients[1], a) == 0);
    assert(do_listen(&clients[1], b) == -EINVAL);
    assert(num_used_listeners() == 1);
    assert(bind_port(1, b, 8088) == -EADDRINUSE);
    assert(num_used_listeners() == 1);
    puts("PASS: bind/bind/listen and bind/listen/bind both exclusive");

    /* Separate namespaces may bind exactly the same port. */
    int linux = socket_for(4);
    assert(bind_port(4, linux, 8088) == 0);
    assert(do_listen(&clients[4], linux) == 0);
    assert(num_used_listeners() == 2);
    puts("PASS: native and Linux compartments independently own port 8088");

    int native_client = socket_for(2), linux_client = socket_for(5);
    assert(connect_port(2, native_client, 8088) == 0);
    assert(streams[streams[clients[2].fds[native_client].idx].peer].owner == 1);
    assert(connect_port(5, linux_client, 8088) == 0);
    assert(streams[streams[clients[5].fds[linux_client].idx].peer].owner == 4);
    puts("PASS: connections route only to same-namespace listeners");

    /* Free the completed connections and verify closing the listener
     * releases its port without touching the other namespace.
     */
    assert(do_close(&clients[2], native_client) == 0);
    assert(do_close(&clients[5], linux_client) == 0);
    assert(do_close(&clients[1], a) == 0);
    assert(num_used_listeners() == 1);
    assert(bind_port(1, b, 8088) == 0);
    assert(bind_port(1, b, 8089) == -EINVAL); /* cannot rebind socket */
    assert(do_listen(&clients[1], b) == 0);
    assert(num_used_listeners() == 2);
    puts("PASS: close and rebind reclaim namespace port reservation");

    assert(do_close(&clients[1], b) == 0);
    assert(do_close(&clients[4], linux) == 0);
    assert(num_used_listeners() == 0);
    assert(num_used_streams() == 0);
    puts("PASS: listener/socket teardown reclaims allocations");

    /* Repeated close/rebind of fresh sockets must not exhaust pools. */
    for (int i = 0; i < 256; i++) {
        int fd = socket_for(1);
        assert(bind_port(1, fd, 8088) == 0);
        assert(do_listen(&clients[1], fd) == 0);
        assert(do_close(&clients[1], fd) == 0);
        assert(num_used_streams() == 0);
        assert(num_used_listeners() == 0);
    }
    puts("PASS: 256 close/rebind cycles do not leak pool allocations");
    return 0;
}
